#ifndef DYNLEX_GRAPHICS_RUNTIME_H
#define DYNLEX_GRAPHICS_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DynlexGraphics DynlexGraphics;
typedef struct DynlexGraphicsPipeline DynlexGraphicsPipeline;
typedef struct DynlexGraphicsTexture DynlexGraphicsTexture;

typedef void (*DynlexGraphicsScrollCallback)(DynlexGraphics *graphics, double horizontal, double vertical);

DynlexGraphics *dynlex_graphics_create(int32_t width, int32_t height, int32_t visible, int32_t fullscreen, const char *title);
void dynlex_graphics_destroy(DynlexGraphics *graphics);

int32_t dynlex_graphics_begin_frame(DynlexGraphics *graphics, double red, double green, double blue, double alpha);
int32_t dynlex_graphics_end_frame(DynlexGraphics *graphics);
void dynlex_graphics_poll_events(void);
int32_t dynlex_graphics_should_close(const DynlexGraphics *graphics);
void dynlex_graphics_request_close(DynlexGraphics *graphics);
int32_t dynlex_graphics_set_title(DynlexGraphics *graphics, const char *title);
void dynlex_graphics_set_visible(DynlexGraphics *graphics, int32_t visible);
int32_t dynlex_graphics_is_visible(const DynlexGraphics *graphics);
void dynlex_graphics_framebuffer_size(const DynlexGraphics *graphics, int32_t *width, int32_t *height);

int32_t dynlex_graphics_key_down(const DynlexGraphics *graphics, int32_t key);
int32_t dynlex_graphics_mouse_button_down(const DynlexGraphics *graphics, int32_t button);
void dynlex_graphics_cursor_position(const DynlexGraphics *graphics, double *horizontal, double *vertical);
void dynlex_graphics_set_cursor_captured(DynlexGraphics *graphics, int32_t captured);
void dynlex_graphics_set_sticky_mouse_buttons(DynlexGraphics *graphics, int32_t enabled);
void dynlex_graphics_set_application_state(DynlexGraphics *graphics, void *state);
void *dynlex_graphics_application_state(const DynlexGraphics *graphics);
void dynlex_graphics_set_scroll_callback(DynlexGraphics *graphics, DynlexGraphicsScrollCallback callback);
double dynlex_graphics_time_since_start(const DynlexGraphics *graphics);
int32_t dynlex_graphics_sleep_milliseconds(int32_t milliseconds);
size_t dynlex_graphics_error_message(char *buffer, size_t capacity);

uint32_t dynlex_graphics_api_version(const DynlexGraphics *graphics);
int32_t dynlex_graphics_portability_enabled(const DynlexGraphics *graphics);
size_t dynlex_graphics_device_name(const DynlexGraphics *graphics, char *buffer, size_t capacity);

int32_t dynlex_graphics_set_orthographic_projection(
	DynlexGraphics *graphics, double left, double right, double top, double bottom, double near_plane, double far_plane
);
int32_t dynlex_graphics_set_perspective_projection(
	DynlexGraphics *graphics, double vertical_field_of_view_degrees, double near_plane, double far_plane
);
void dynlex_graphics_reset_camera(DynlexGraphics *graphics);
void dynlex_graphics_set_depth_testing(DynlexGraphics *graphics, int32_t enabled);
void dynlex_graphics_translate_camera(DynlexGraphics *graphics, double horizontal, double vertical, double depth);
int32_t dynlex_graphics_rotate_camera(
	DynlexGraphics *graphics, double degrees, double horizontal_axis, double vertical_axis, double depth_axis
);
void dynlex_graphics_reset_model(DynlexGraphics *graphics);
void dynlex_graphics_translate_model(DynlexGraphics *graphics, double horizontal, double vertical, double depth);
void dynlex_graphics_scale_model(DynlexGraphics *graphics, double horizontal, double vertical, double depth);
int32_t dynlex_graphics_rotate_model(
	DynlexGraphics *graphics, double degrees, double horizontal_axis, double vertical_axis, double depth_axis
);
int32_t dynlex_graphics_push_model(DynlexGraphics *graphics);
int32_t dynlex_graphics_pop_model(DynlexGraphics *graphics);

int32_t dynlex_graphics_begin_triangles(DynlexGraphics *graphics, DynlexGraphicsTexture *texture);
void dynlex_graphics_set_vertex_color(DynlexGraphics *graphics, int32_t red, int32_t green, int32_t blue, int32_t alpha);
void dynlex_graphics_set_texture_coordinates(DynlexGraphics *graphics, double horizontal, double vertical);
int32_t dynlex_graphics_append_vertex_2d(DynlexGraphics *graphics, double horizontal, double vertical);
int32_t dynlex_graphics_append_vertex_3d(DynlexGraphics *graphics, double horizontal, double vertical, double depth);
int32_t dynlex_graphics_finish_triangles(DynlexGraphics *graphics);
int32_t dynlex_graphics_draw_rectangle(
	DynlexGraphics *graphics, double horizontal, double vertical, double width, double height, int32_t red, int32_t green,
	int32_t blue, int32_t alpha
);
int32_t dynlex_graphics_draw_textured_rectangle(
	DynlexGraphics *graphics, DynlexGraphicsTexture *texture, double left, double top, double right, double bottom, int32_t red,
	int32_t green, int32_t blue, int32_t alpha
);

DynlexGraphicsTexture *
dynlex_graphics_texture_create_rgba8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels);
DynlexGraphicsTexture *
dynlex_graphics_texture_create_rgb8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels);
DynlexGraphicsTexture *
dynlex_graphics_texture_create_red8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels);
int32_t dynlex_graphics_texture_update(DynlexGraphicsTexture *texture, int32_t width, int32_t height, const void *pixels);
int32_t dynlex_graphics_texture_destroy(DynlexGraphicsTexture *texture);

DynlexGraphicsPipeline *
dynlex_graphics_pipeline_create(DynlexGraphics *graphics, const char *vertex_path, const char *fragment_path);
int32_t dynlex_graphics_pipeline_destroy(DynlexGraphicsPipeline *pipeline);
int32_t dynlex_graphics_use_pipeline(DynlexGraphics *graphics, DynlexGraphicsPipeline *pipeline);
int32_t dynlex_graphics_set_uniform_float(DynlexGraphics *graphics, int32_t binding, double value);
int32_t dynlex_graphics_draw_fullscreen(DynlexGraphics *graphics);

#ifdef __cplusplus
}
#endif

#endif
