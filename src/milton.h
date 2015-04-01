#pragma once

// Rename types for convenience
typedef int8_t      int8;
typedef uint8_t     uint8;
typedef int16_t     int16;
typedef uint16_t    uint16;
typedef int32_t     int32;
typedef uint32_t    uint32;
typedef int64_t     int64;
typedef uint64_t    uint64;
typedef int32_t     bool32;

#include "vector.generated.h"  // Generated by metaprogram

typedef v2l StrokePoint;

typedef struct
{
    StrokePoint* points;
    int64        num_points;
} StoredStroke;

typedef struct
{
    int32_t     full_width;             // Dimensions of the raster
    int32_t     full_height;
    uint8_t     bytes_per_pixel;
    uint8_t*    raster_buffer;
    size_t      raster_buffer_size;

    v2l screen_size;

    // Maps screen_size to a rectangle in our infinite canvas.
    // view_center + (view_scale * screen_size)
    v2l view_center;
    int64 view_scale;

    // Current stroke.
    StrokePoint stroke_points[4096 * 4096];
    int64       num_stroke_points;

    // Before we get our nice spacial partition...
    StoredStroke    stored_strokes[4096 * 4096];
    int64           num_stored_strokes;

    // Heap
    Arena*      root_arena;     // Persistent memory.
    Arena*      frame_arena;    // Gets reset after every call to milton_update().
    // Debug:
} MiltonState;

typedef struct
{
    bool32 full_refresh;
    v2l* brush;
} MiltonInput;

typedef struct
{
    v2l top_left;
    v2l bot_right;
} Rectl;

static void milton_init(MiltonState* milton_state)
{
    // Allocate enough memory for the maximum possible supported resolution. As
    // of now, it seems like future 8k displays will adopt this resolution.
    milton_state->full_width      = 7680;
    milton_state->full_height     = 4320;
    milton_state->bytes_per_pixel = 4;
    milton_state->view_scale      = 1000 * 1000 * 1000;  // A billion.
    // A view_scale of a billion puts the initial scale at one meter.

    // View center can be at ( 0, 0 ) for debuggability
    //milton_state->view_center     = make_v2l ( ((uint64)1 << 63) / 2, ((uint64)1 << 63) / 2 );

    int closest_power_of_two = (1 << 27);  // Ceiling of log2(width * height * bpp)
    milton_state->raster_buffer_size = closest_power_of_two;

    milton_state->raster_buffer = arena_alloc_array(milton_state->root_arena,
            milton_state->raster_buffer_size, uint8_t);
}

static Rectl bounding_rect_for_stroke(StrokePoint points[], int64 num_points)
{
    assert (num_points > 0);

    v2l top_left = points[0];
    v2l bot_right = points[0];

    for (int64 i = 1; i < num_points; ++i)
    {
        v2l point = points[i];
        if (point.x < top_left.x) top_left.x = point.x;
        if (point.y > top_left.y) top_left.x = point.x;
        if (point.x > bot_right.x) bot_right.x = point.x;
        if (point.y > bot_right.y) bot_right.y = point.y;
    }
    Rectl rect = { top_left, bot_right };
    return rect;
}

static v2l canvas_to_raster(MiltonState* milton_state, v2l canvas_point)
{
    // Move from infinite canvas to raster
    v2l point = canvas_point;
    point = add_v2l     (point, milton_state->view_center);
    point = invscale_v2l(point, milton_state->view_scale);
    return point;
}

static void rasterize_stroke(MiltonState* milton_state, v2l* points, int64 num_points)
{
    uint32* pixels = (uint32_t*)milton_state->raster_buffer;

    for (int64 i = 0; i < num_points; ++i)
    {
        v2l canvas_point = points[i];

        v2l point = canvas_to_raster(milton_state, canvas_point);

        if (point.w >= milton_state->screen_size.w || point.h >= milton_state->screen_size.h)
            continue;
        int64 index = point.y * milton_state->screen_size.w + point.x;

        assert ( milton_state->raster_buffer_size < ((uint64)1 << 63));
        assert (index < (int64)milton_state->raster_buffer_size);

        pixels[index] = 0xff0000ff;

    }
}

// Returns non-zero if the raster buffer was modified by this update.
static bool32 milton_update(MiltonState* milton_state, MiltonInput* input)
{
    bool32 updated = 0;
    // Do a complete re-rasterization.
    if (input->full_refresh || 1)
    {
        uint32* pixels = (uint32_t*)milton_state->raster_buffer;
        for (int y = 0; y < milton_state->screen_size.h; ++y)
        {
            for (int x = 0; x < milton_state->screen_size.w; ++x)
            {
                *pixels++ = 0xff000000;
            }
        }
        updated = 1;
    }
    if (input->brush)
    {
        v2l in_point = *input->brush;
        // Move to infinite canvas
        v2l canvas_point = scale_v2l(in_point, milton_state->view_scale);
        canvas_point = sub_v2l(canvas_point, milton_state->view_center);

        // Add to current stroke.
        milton_state->stroke_points[milton_state->num_stroke_points++] = canvas_point;

        rasterize_stroke(milton_state, milton_state->stroke_points, milton_state->num_stroke_points);
        updated = 1;
    }
    else if (milton_state->num_stroke_points > 0)
    {
        // Push stroke to history.

        StoredStroke stored;
        stored.points = arena_alloc_array(milton_state->root_arena,
                milton_state->num_stroke_points, StrokePoint);
        memcpy(stored.points, milton_state->stroke_points,
                milton_state->num_stroke_points * sizeof(StrokePoint));
        stored.num_points = milton_state->num_stroke_points;

        milton_state->stored_strokes[milton_state->num_stored_strokes++] = stored;

        milton_state->num_stroke_points = 0;
    }

    for (int i = 0; i < milton_state->num_stored_strokes; ++i)
    {
        StoredStroke* stored = &milton_state->stored_strokes[i];
        rasterize_stroke(milton_state, stored->points, stored->num_points);
    }



    return updated;
}