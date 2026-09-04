#include "platformFeatureTest.h"

#include "graphicsRuntimeInternal.h"

#include "runtimeError.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

static atomic_flag glfw_lock = ATOMIC_FLAG_INIT;
static uint32_t glfw_users;

static void lock_glfw(void) {
	while (atomic_flag_test_and_set_explicit(&glfw_lock, memory_order_acquire)) {
	}
}

static void unlock_glfw(void) { atomic_flag_clear_explicit(&glfw_lock, memory_order_release); }

static void set_glfw_error(const char *operation) {
	const char *description = NULL;
	int code = glfwGetError(&description);
	char message[512];
	if (description == NULL)
		snprintf(message, sizeof(message), "%s failed (GLFW error %d)", operation, code);
	else
		snprintf(message, sizeof(message), "%s failed: %s (GLFW error %d)", operation, description, code);
	dynlex_runtime_set_error(message);
}

#ifdef __APPLE__
static bool configure_moltenvk_driver(void) {
	if (getenv("VK_ADD_DRIVER_FILES") != NULL || getenv("VK_DRIVER_FILES") != NULL || getenv("VK_ICD_FILENAMES") != NULL)
		return true;
	uint32_t size = 0;
	if (_NSGetExecutablePath(NULL, &size) != -1 || size == 0) {
		dynlex_runtime_set_error("failed to determine the executable path for MoltenVK discovery");
		return false;
	}
	char *executable = malloc(size);
	if (executable == NULL) {
		dynlex_runtime_set_error("failed to allocate the executable path for MoltenVK discovery");
		return false;
	}
	if (_NSGetExecutablePath(executable, &size) != 0) {
		free(executable);
		dynlex_runtime_set_error("failed to determine the executable path for MoltenVK discovery");
		return false;
	}
	char resolved[PATH_MAX];
	if (realpath(executable, resolved) == NULL) {
		free(executable);
		dynlex_runtime_set_error("failed to resolve the executable path for MoltenVK discovery");
		return false;
	}
	free(executable);
	char *separator = strrchr(resolved, '/');
	if (separator == NULL) {
		dynlex_runtime_set_error("the executable path has no directory for MoltenVK discovery");
		return false;
	}
	separator[1] = '\0';
	const char manifest_name[] = "MoltenVK_icd.json";
	if (strlen(resolved) + sizeof(manifest_name) > sizeof(resolved)) {
		dynlex_runtime_set_error("the MoltenVK manifest path is too long");
		return false;
	}
	strcat(resolved, manifest_name);
	if (access(resolved, R_OK) != 0) {
		if (errno == ENOENT)
			return true;
		dynlex_runtime_set_error("the bundled MoltenVK_icd.json beside the executable is not readable");
		return false;
	}
	if (setenv("VK_ADD_DRIVER_FILES", resolved, 0) != 0) {
		dynlex_runtime_set_error("failed to configure the bundled MoltenVK driver manifest");
		return false;
	}
	return true;
}
#else
static bool configure_moltenvk_driver(void) { return true; }
#endif

static bool retain_glfw(void) {
	lock_glfw();
	if (glfw_users == 0) {
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
		glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
		if (glfwInit() != GLFW_TRUE) {
			set_glfw_error("initializing the graphics window system");
			unlock_glfw();
			return false;
		}
	}
	++glfw_users;
	unlock_glfw();
	return true;
}

static void release_glfw(void) {
	lock_glfw();
	if (--glfw_users == 0)
		glfwTerminate();
	unlock_glfw();
}

static void framebuffer_resized(GLFWwindow *window, int width, int height) {
	(void)width;
	(void)height;
	DynlexGraphics *graphics = glfwGetWindowUserPointer(window);
	graphics->swapchain_dirty = true;
}

static void scroll_received(GLFWwindow *window, double horizontal, double vertical) {
	DynlexGraphics *graphics = glfwGetWindowUserPointer(window);
	if (graphics->scroll_callback != NULL)
		graphics->scroll_callback(graphics, horizontal, vertical);
}

void dynlex_graphics_matrix_identity(float matrix[16]) {
	memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

void dynlex_graphics_matrix_multiply(float result[16], const float left[16], const float right[16]) {
	float product[16];
	for (uint32_t column = 0; column < 4; ++column) {
		for (uint32_t row = 0; row < 4; ++row) {
			product[column * 4 + row] = 0.0f;
			for (uint32_t inner = 0; inner < 4; ++inner)
				product[column * 4 + row] += left[inner * 4 + row] * right[column * 4 + inner];
		}
	}
	memcpy(result, product, sizeof(product));
}

static void apply_transform(float matrix[16], const float transform[16]) {
	dynlex_graphics_matrix_multiply(matrix, matrix, transform);
}

static int32_t rotate(float matrix[16], double degrees, double x_value, double y_value, double z_value) {
	double magnitude = sqrt(x_value * x_value + y_value * y_value + z_value * z_value);
	if (!isfinite(degrees) || !isfinite(magnitude) || magnitude == 0.0) {
		dynlex_runtime_set_error("a graphics rotation requires a finite angle and a non-zero finite axis");
		return 0;
	}
	float x = (float)(x_value / magnitude);
	float y = (float)(y_value / magnitude);
	float z = (float)(z_value / magnitude);
	float radians = (float)(degrees * 0.017453292519943295769);
	float cosine = cosf(radians);
	float sine = sinf(radians);
	float one_minus_cosine = 1.0f - cosine;
	float rotation[16] = {
		cosine + x * x * one_minus_cosine,
		y * x * one_minus_cosine + z * sine,
		z * x * one_minus_cosine - y * sine,
		0.0f,
		x * y * one_minus_cosine - z * sine,
		cosine + y * y * one_minus_cosine,
		z * y * one_minus_cosine + x * sine,
		0.0f,
		x * z * one_minus_cosine + y * sine,
		y * z * one_minus_cosine - x * sine,
		cosine + z * z * one_minus_cosine,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f,
	};
	apply_transform(matrix, rotation);
	return 1;
}

DynlexGraphics *dynlex_graphics_create(int32_t width, int32_t height, int32_t visible, int32_t fullscreen, const char *title) {
	dynlex_runtime_clear_error();
	if ((visible != 0 && visible != 1) || (fullscreen != 0 && fullscreen != 1) || title == NULL ||
		(fullscreen == 0 && (width <= 0 || height <= 0)) || (fullscreen == 1 && (width != 0 || height != 0))) {
		dynlex_runtime_set_error(
			"creating graphics requires a title, boolean flags, positive windowed dimensions, and zero fullscreen dimensions"
		);
		return NULL;
	}
	if (!configure_moltenvk_driver() || !retain_glfw())
		return NULL;
	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, visible == 1 ? GLFW_TRUE : GLFW_FALSE);
	GLFWmonitor *monitor = fullscreen == 1 ? glfwGetPrimaryMonitor() : NULL;
	if (fullscreen == 1) {
		const GLFWvidmode *mode = monitor == NULL ? NULL : glfwGetVideoMode(monitor);
		if (mode == NULL) {
			set_glfw_error("querying the primary monitor video mode");
			release_glfw();
			return NULL;
		}
		width = mode->width;
		height = mode->height;
	}
	DynlexGraphics *graphics = calloc(1, sizeof(*graphics));
	if (graphics == NULL) {
		dynlex_runtime_set_error("failed to allocate the graphics context");
		release_glfw();
		return NULL;
	}
	graphics->window = glfwCreateWindow(width, height, title, monitor, NULL);
	if (graphics->window == NULL) {
		set_glfw_error("creating the Vulkan window");
		free(graphics);
		release_glfw();
		return NULL;
	}
	glfwSetWindowUserPointer(graphics->window, graphics);
	glfwSetFramebufferSizeCallback(graphics->window, framebuffer_resized);
	glfwSetScrollCallback(graphics->window, scroll_received);
	dynlex_graphics_matrix_identity(graphics->projection);
	dynlex_graphics_matrix_identity(graphics->camera);
	dynlex_graphics_matrix_identity(graphics->model);
	graphics->current_vertex.color[0] = 1.0f;
	graphics->current_vertex.color[1] = 1.0f;
	graphics->current_vertex.color[2] = 1.0f;
	graphics->current_vertex.color[3] = 1.0f;
	if (!dynlex_graphics_create_vulkan(graphics)) {
		dynlex_graphics_destroy_vulkan(graphics);
		glfwDestroyWindow(graphics->window);
		free(graphics);
		release_glfw();
		return NULL;
	}
	return graphics;
}

void dynlex_graphics_destroy(DynlexGraphics *graphics) {
	if (graphics == NULL)
		return;
	dynlex_graphics_destroy_vulkan(graphics);
	glfwDestroyWindow(graphics->window);
	free(graphics);
	release_glfw();
}

void dynlex_graphics_poll_events(void) { glfwPollEvents(); }

int32_t dynlex_graphics_should_close(const DynlexGraphics *graphics) {
	return glfwWindowShouldClose(graphics->window) == GLFW_TRUE;
}

void dynlex_graphics_request_close(DynlexGraphics *graphics) { glfwSetWindowShouldClose(graphics->window, GLFW_TRUE); }

int32_t dynlex_graphics_set_title(DynlexGraphics *graphics, const char *title) {
	if (title == NULL) {
		dynlex_runtime_set_error("a graphics window title cannot be null");
		return 0;
	}
	glfwSetWindowTitle(graphics->window, title);
	return 1;
}

void dynlex_graphics_set_visible(DynlexGraphics *graphics, int32_t visible) {
	if (visible == 0)
		glfwHideWindow(graphics->window);
	else
		glfwShowWindow(graphics->window);
}

int32_t dynlex_graphics_is_visible(const DynlexGraphics *graphics) {
	return glfwGetWindowAttrib(graphics->window, GLFW_VISIBLE) == GLFW_TRUE;
}

void dynlex_graphics_framebuffer_size(const DynlexGraphics *graphics, int32_t *width, int32_t *height) {
	glfwGetFramebufferSize(graphics->window, width, height);
}

int32_t dynlex_graphics_key_down(const DynlexGraphics *graphics, int32_t key) {
	int state = glfwGetKey(graphics->window, key);
	return state == GLFW_PRESS || state == GLFW_REPEAT;
}

int32_t dynlex_graphics_mouse_button_down(const DynlexGraphics *graphics, int32_t button) {
	return glfwGetMouseButton(graphics->window, button) == GLFW_PRESS;
}

void dynlex_graphics_cursor_position(const DynlexGraphics *graphics, double *horizontal, double *vertical) {
	glfwGetCursorPos(graphics->window, horizontal, vertical);
}

void dynlex_graphics_set_cursor_captured(DynlexGraphics *graphics, int32_t captured) {
	glfwSetInputMode(graphics->window, GLFW_CURSOR, captured == 0 ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void dynlex_graphics_set_sticky_mouse_buttons(DynlexGraphics *graphics, int32_t enabled) {
	glfwSetInputMode(graphics->window, GLFW_STICKY_MOUSE_BUTTONS, enabled == 0 ? GLFW_FALSE : GLFW_TRUE);
}

void dynlex_graphics_set_application_state(DynlexGraphics *graphics, void *state) { graphics->application_state = state; }

void *dynlex_graphics_application_state(const DynlexGraphics *graphics) { return graphics->application_state; }

void dynlex_graphics_set_scroll_callback(DynlexGraphics *graphics, DynlexGraphicsScrollCallback callback) {
	graphics->scroll_callback = callback;
}

double dynlex_graphics_time_since_start(const DynlexGraphics *graphics) {
	(void)graphics;
	return glfwGetTime();
}

int32_t dynlex_graphics_sleep_milliseconds(int32_t milliseconds) {
	if (milliseconds < 0) {
		dynlex_runtime_set_error("a graphics sleep duration cannot be negative");
		return 0;
	}
#ifdef _WIN32
	Sleep((DWORD)milliseconds);
#else
	struct timespec remaining = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (long)(milliseconds % 1000) * 1000000L,
	};
	while (nanosleep(&remaining, &remaining) != 0) {
		if (errno == EINTR)
			continue;
		dynlex_runtime_set_error("waiting for the graphics sleep duration failed");
		return 0;
	}
#endif
	return 1;
}

size_t dynlex_graphics_error_message(char *buffer, size_t capacity) { return dynlex_runtime_error_message(buffer, capacity); }

uint32_t dynlex_graphics_api_version(const DynlexGraphics *graphics) { return graphics->physical_properties.apiVersion; }

int32_t dynlex_graphics_portability_enabled(const DynlexGraphics *graphics) { return graphics->portability_enabled; }

size_t dynlex_graphics_device_name(const DynlexGraphics *graphics, char *buffer, size_t capacity) {
	size_t length = strlen(graphics->physical_properties.deviceName);
	if (buffer != NULL && capacity > 0) {
		size_t copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(buffer, graphics->physical_properties.deviceName, copied);
		buffer[copied] = '\0';
	}
	return length;
}

int32_t dynlex_graphics_set_orthographic_projection(
	DynlexGraphics *graphics, double left, double right, double top, double bottom, double near_plane, double far_plane
) {
	if (!(isfinite(left) && isfinite(right) && isfinite(top) && isfinite(bottom) && isfinite(near_plane) && isfinite(far_plane)
		) ||
		left == right || top == bottom || near_plane == far_plane) {
		dynlex_runtime_set_error("an orthographic projection requires finite, distinct bounds");
		return 0;
	}
	dynlex_graphics_matrix_identity(graphics->projection);
	graphics->projection[0] = (float)(2.0 / (right - left));
	graphics->projection[5] = (float)(2.0 / (bottom - top));
	graphics->projection[10] = (float)(1.0 / (near_plane - far_plane));
	graphics->projection[12] = (float)(-(right + left) / (right - left));
	graphics->projection[13] = (float)(-(bottom + top) / (bottom - top));
	graphics->projection[14] = (float)(near_plane / (near_plane - far_plane));
	return 1;
}

int32_t dynlex_graphics_set_perspective_projection(
	DynlexGraphics *graphics, double vertical_field_of_view_degrees, double near_plane, double far_plane
) {
	if (!(isfinite(vertical_field_of_view_degrees) && isfinite(near_plane) && isfinite(far_plane)) ||
		vertical_field_of_view_degrees <= 0.0 || vertical_field_of_view_degrees >= 180.0 || near_plane <= 0.0 ||
		far_plane <= near_plane || graphics->swapchain_extent.height == 0) {
		dynlex_runtime_set_error("a perspective projection requires a field of view between 0 and 180 and valid clip planes");
		return 0;
	}
	double aspect = (double)graphics->swapchain_extent.width / (double)graphics->swapchain_extent.height;
	double scale = 1.0 / tan(vertical_field_of_view_degrees * 0.0087266462599716478846);
	memset(graphics->projection, 0, sizeof(graphics->projection));
	graphics->projection[0] = (float)(scale / aspect);
	graphics->projection[5] = (float)scale;
	graphics->projection[10] = (float)(far_plane / (near_plane - far_plane));
	graphics->projection[11] = -1.0f;
	graphics->projection[14] = (float)((near_plane * far_plane) / (near_plane - far_plane));
	return 1;
}

void dynlex_graphics_set_depth_testing(DynlexGraphics *graphics, int32_t enabled) { graphics->depth_testing = enabled != 0; }

void dynlex_graphics_reset_camera(DynlexGraphics *graphics) { dynlex_graphics_matrix_identity(graphics->camera); }

void dynlex_graphics_translate_camera(DynlexGraphics *graphics, double horizontal, double vertical, double depth) {
	float translation[16];
	dynlex_graphics_matrix_identity(translation);
	translation[12] = (float)horizontal;
	translation[13] = (float)vertical;
	translation[14] = (float)depth;
	apply_transform(graphics->camera, translation);
}

int32_t dynlex_graphics_rotate_camera(
	DynlexGraphics *graphics, double degrees, double horizontal_axis, double vertical_axis, double depth_axis
) {
	return rotate(graphics->camera, degrees, horizontal_axis, vertical_axis, depth_axis);
}

void dynlex_graphics_reset_model(DynlexGraphics *graphics) { dynlex_graphics_matrix_identity(graphics->model); }

void dynlex_graphics_translate_model(DynlexGraphics *graphics, double horizontal, double vertical, double depth) {
	float translation[16];
	dynlex_graphics_matrix_identity(translation);
	translation[12] = (float)horizontal;
	translation[13] = (float)vertical;
	translation[14] = (float)depth;
	apply_transform(graphics->model, translation);
}

void dynlex_graphics_scale_model(DynlexGraphics *graphics, double horizontal, double vertical, double depth) {
	float scale[16];
	dynlex_graphics_matrix_identity(scale);
	scale[0] = (float)horizontal;
	scale[5] = (float)vertical;
	scale[10] = (float)depth;
	apply_transform(graphics->model, scale);
}

int32_t dynlex_graphics_rotate_model(
	DynlexGraphics *graphics, double degrees, double horizontal_axis, double vertical_axis, double depth_axis
) {
	return rotate(graphics->model, degrees, horizontal_axis, vertical_axis, depth_axis);
}

int32_t dynlex_graphics_push_model(DynlexGraphics *graphics) {
	if (graphics->model_stack_size == DYNLEX_GRAPHICS_MODEL_STACK) {
		dynlex_runtime_set_error("the graphics model transform stack is full");
		return 0;
	}
	memcpy(graphics->model_stack[graphics->model_stack_size++], graphics->model, sizeof(graphics->model));
	return 1;
}

int32_t dynlex_graphics_pop_model(DynlexGraphics *graphics) {
	if (graphics->model_stack_size == 0) {
		dynlex_runtime_set_error("the graphics model transform stack is empty");
		return 0;
	}
	memcpy(graphics->model, graphics->model_stack[--graphics->model_stack_size], sizeof(graphics->model));
	return 1;
}
