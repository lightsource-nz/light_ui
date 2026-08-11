#include <light_ui.h>

#include "light_ui_internal.h"

#include <string.h>

// draws `text` at (x, y) truncated to fit both max_width and the canvas, in whatever
// rend's color_fg currently is.
//
// rend_draw_text() neither clips nor bounds its width -- it walks glyphs off the end of the
// canvas straight into _set_pixel(), which for anything past the buffer is an out-of-bounds
// write, not just a cosmetic overflow. so the fit has to be computed here. fonts are
// fixed-pitch (rend_font_t has a single char_width), which makes that arithmetic rather
// than a measurement pass
static void _draw_text_fitted(struct ui_context *ui, int16_t x, int16_t y,
                                const uint8_t *text, int16_t max_width)
{
        rend_context_t *render = _ui_render(ui);
        const rend_font_t *font = render->font;
        if(!font || !text || !font->char_width)
                return;
        // a glyph row is drawn from its top-left corner downward and rightward, and there
        // is no partial-glyph path -- so anything that doesn't start on-canvas, or whose
        // full height doesn't fit, is dropped rather than half-drawn. the x >= dim_x half
        // of that also has to be rejected before the fit_canvas division below, where a
        // non-positive remaining width would wrap into a huge size_t and defeat the very
        // truncation it is there to compute
        if(x < 0 || y < 0 || x >= (int16_t)render->dim_x
                        || y + font->char_height > (int16_t)render->dim_y)
                return;

        size_t len = strlen((const char *)text);
        size_t fit_widget = (size_t)(max_width > 0 ? max_width : 0) / font->char_width;
        size_t fit_canvas = (size_t)((int16_t)render->dim_x - x) / font->char_width;
        if(len > fit_widget) len = fit_widget;
        if(len > fit_canvas) len = fit_canvas;
        if(len > LIGHT_UI_TEXT_MAX - 1) len = LIGHT_UI_TEXT_MAX - 1;
        if(!len)
                return;

        uint8_t buf[LIGHT_UI_TEXT_MAX];
        memcpy(buf, text, len);
        buf[len] = '\0';
        rend_draw_text(render, (rend_point2d) { (uint16_t)x, (uint16_t)y }, buf);
}

// horizontally centres a string of `len` glyphs within [x0, x1], never left of x0
static int16_t _centre_x(const rend_font_t *font, int16_t x0, int16_t x1, size_t len)
{
        int32_t avail = (int32_t)x1 - x0 + 1;
        int32_t used = (int32_t)len * font->char_width;
        if(used >= avail)
                return x0;
        return (int16_t)(x0 + (avail - used) / 2);
}

static void _paint_window(struct ui_context *ui, struct ui_window *win)
{
        struct ui_rect r = win->widget.rect;
        if(!_ui_clip_to_canvas(ui, &r))
                return;

        rend_context_t *render = _ui_render(ui);
        if(win->border) {
                if(win->corner_radius)
                        rend_draw_rect_rounded(render,
                                (rend_point2d) { (uint16_t)r.x0, (uint16_t)r.y0 },
                                (rend_point2d) { (uint16_t)r.x1, (uint16_t)r.y1 },
                                win->corner_radius, false);
                else
                        rend_draw_rect(render,
                                (rend_point2d) { (uint16_t)r.x0, (uint16_t)r.y0 },
                                (rend_point2d) { (uint16_t)r.x1, (uint16_t)r.y1 }, false);
        }

        const rend_font_t *font = render->font;
        if(!win->title || !font)
                return;

        // title sits just inside the frame, at the very top, with a separator line under it --
        // the same header band light_ui_window_layout_stack() reserves, so the two must agree
        // on its height (char_height + 2) and on where it starts.
        //
        // a rounded corner is cleared SIDEWAYS here, not downward. the title is one short
        // string, so shifting it right by however far the arc reaches in at its topmost row
        // costs a few characters of a line that has room to spare, where dropping the header
        // below the curve would cost a band the depth of the radius across the whole window.
        // the indent is taken at the title's TOP row because that is where the arc is furthest
        // in -- clearing that clears every row beneath it
        int16_t inset = win->border ? 1 : 0;
        int16_t ty = r.y0 + inset;
        int16_t indent = _ui_corner_indent(win, (int16_t)(win->corner_radius - inset));
        int16_t tx = r.x0 + indent + inset + 1;
        // the top-RIGHT arc mirrors the top-left one, so the line the title has to fit in is
        // shortened at both ends
        _draw_text_fitted(ui, tx, ty, win->title, (r.x1 - indent - inset) - tx + 1);

        // the separator sits char_height lower, where the arc has already come most of the
        // way back out -- so it gets its own, much smaller, indent rather than the title's
        int16_t sep_y = ty + font->char_height;
        int16_t sep_indent = _ui_corner_indent(win,
                        (int16_t)(win->corner_radius - (sep_y - r.y0)));
        if(sep_y <= r.y1 && sep_y >= 0)
                rend_draw_line(render,
                        (rend_point2d) { (uint16_t)(r.x0 + sep_indent + inset), (uint16_t)sep_y },
                        (rend_point2d) { (uint16_t)(r.x1 - sep_indent - inset), (uint16_t)sep_y }, true);
}

static void _paint_button(struct ui_context *ui, struct ui_button *btn)
{
        struct ui_rect r = btn->widget.rect;
        if(!_ui_clip_to_canvas(ui, &r))
                return;

        rend_context_t *render = _ui_render(ui);
        bool focused = ui->focused == &btn->widget;

        rend_point2d p0 = { (uint16_t)r.x0, (uint16_t)r.y0 };
        rend_point2d p1 = { (uint16_t)r.x1, (uint16_t)r.y1 };

        // rend's draw calls take a const context but read color_fg from it, and there is no
        // per-call colour argument -- so inverting the focused button means swapping the
        // context's own colours around the calls and putting them back. works uniformly for
        // 1bpp and RGB565, since both go through the same _set_pixel() colour path
        uint16_t saved_fg = render->color_fg;
        rend_draw_rect(render, p0, p1, focused);
        if(focused)
                render->color_fg = render->color_bg;

        const rend_font_t *font = render->font;
        if(font && btn->label) {
                // the border occupies the outermost pixel ring; the label goes inside it
                int16_t inner_x0 = r.x0 + 1, inner_x1 = r.x1 - 1;
                int16_t inner_h = r.y1 - r.y0 - 1;
                size_t len = strlen((const char *)btn->label);
                int16_t tx = _centre_x(font, inner_x0, inner_x1, len);
                int16_t ty = (int16_t)(r.y0 + 1 + (inner_h - font->char_height) / 2);
                if(ty < r.y0 + 1)
                        ty = r.y0 + 1;
                _draw_text_fitted(ui, tx, ty, btn->label, inner_x1 - inner_x0 + 1);
        }

        render->color_fg = saved_fg;
        // TODO give disabled buttons a distinct appearance. rend_draw_line() accepts a
        // `solid` flag but ignores it, so there is no dashed border to reach for yet, and
        // anything else (greyed text) needs a colour model this 1bpp path doesn't have
}

static void _paint_label(struct ui_context *ui, struct ui_label *lbl)
{
        struct ui_rect r = lbl->widget.rect;
        if(!_ui_clip_to_canvas(ui, &r))
                return;
        _draw_text_fitted(ui, r.x0, r.y0, lbl->text, r.x1 - r.x0 + 1);
}

void _ui_paint_widget(struct ui_context *ui, struct ui_widget *w)
{
        if(!w->visible)
                return;

        switch(w->type) {
        case UI_WIDGET_WINDOW:
                _paint_window(ui, to_ui_window(w));
                break;
        case UI_WIDGET_BUTTON:
                _paint_button(ui, to_ui_button(w));
                break;
        case UI_WIDGET_LABEL:
                _paint_label(ui, to_ui_label(w));
                break;
        default:
                light_warn("unknown widget type %d", w->type);
                break;
        }

        // children after the parent, and in sibling order -- both are "later draws on top",
        // which is what puts a button inside its window's frame rather than under it
        for(struct ui_widget *c = w->first_child; c; c = c->next_sibling)
                _ui_paint_widget(ui, c);
}
