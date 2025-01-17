#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <SDL.h>

#include "nopemd.h"

struct player {
    struct nmd_ctx *nmd_ctx;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int texture_width;
    int texture_height;

    int framerate[2];
    double duration_f;
    int64_t duration;
    int64_t duration_i;
    int width;
    int height;
    int aspect_ratio[2];
    int viewport[4];

    int64_t clock_off;
    int64_t frame_ts;
    int64_t frame_index;
    double  frame_time;
    int paused;
    int seeking;
    int next_frame_requested;
    int mouse_down;
    int fullscreen;
};

static int clipi32(int v, int min, int max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static int64_t clipi64(int64_t v, int64_t min, int64_t max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static int64_t gettime_relative(void)
{
    return SDL_GetTicks64() * 1000;
}

static void set_frame_ts(struct player *p, int64_t frame_ts)
{
    p->frame_ts = frame_ts;
    p->frame_index = llrint((p->frame_ts * p->framerate[0]) / (double)(p->framerate[1] * 1000000));
    p->frame_time = (p->frame_index * p->framerate[1]) / (double)p->framerate[0];
}

static void set_frame_index(struct player *p, int64_t frame_index)
{
    p->frame_index = frame_index;
    p->frame_time = (p->frame_index * p->framerate[1]) / (double)p->framerate[0];
    p->frame_ts = llrint(p->frame_index * p->framerate[1] * 1000000 / (double)p->framerate[0]);
}

static void update_time(struct player *p, int64_t seek_at)
{
    if (seek_at >= 0) {
        p->seeking = 1;
        p->clock_off = gettime_relative() - seek_at;
        set_frame_ts(p, seek_at);
        printf("Seek to %f/%f (%d%%)\n", p->frame_time, p->duration_f, (int)(p->frame_time/p->duration_f*100));
        return;
    }

    if (!p->paused && !p->mouse_down) {
        const int64_t now = gettime_relative();
        if (p->clock_off == INT64_MIN || now - p->clock_off > p->duration) {
            p->seeking = 1;
            p->clock_off = now;
        }

        set_frame_ts(p, now - p->clock_off);
    }
}

static void reset_running_time(struct player *p)
{
    p->clock_off = gettime_relative() - p->frame_ts;
}

static void get_viewport(int width, int height, const int *aspect_ratio, int *vp)
{
    vp[2] = width;
    vp[3] = width * aspect_ratio[1] / (double)aspect_ratio[0];
    if (vp[3] > height) {
        vp[3] = height;
        vp[2] = height * aspect_ratio[0] / (double)aspect_ratio[1];
    }
    vp[0] = (width  - vp[2]) / 2.0;
    vp[1] = (height - vp[3]) / 2.0;
}

static void size_callback(struct player *p, int width, int height)
{
    p->width = width;
    p->height = height;
    get_viewport(p->width, p->height, p->aspect_ratio, p->viewport);
}

static void seek_event(struct player *p, int x)
{
    const int *vp = p->viewport;
    const int pos = clipi32(x - vp[0], 0, vp[2]);
    const int64_t seek_at64 = p->duration * pos / vp[2];
    update_time(p, clipi64(seek_at64, 0, p->duration));
}

static void mouse_buttondown_callback(struct player *p, SDL_MouseButtonEvent *event)
{
    p->mouse_down = 1;
    seek_event(p, event->x);
}

static void mouse_buttonup_callback(struct player *p, SDL_MouseButtonEvent *event)
{
    p->mouse_down = 0;
    p->clock_off = gettime_relative() - p->frame_ts;
}

static void mouse_pos_callback(struct player *p, SDL_MouseMotionEvent *event)
{
    if (p->mouse_down)
        seek_event(p, event->x);
}

static void render(struct player *p)
{
    const SDL_Rect dst = {
        .x = p->viewport[0],
        .y = p->viewport[1],
        .w = p->viewport[2],
        .h = p->viewport[3],
    };

    struct nmd_frame *frame = NULL;
    if (p->next_frame_requested) {
        int ret = nmd_get_next_frame(p->nmd_ctx, &frame);
        if (ret == NMD_RET_NEWFRAME) {
            printf("Stepped to frame t=%f\n", frame->ts);
            p->frame_ts = frame->ts * 1000000;
            p->frame_index = llrint((p->frame_ts * p->framerate[0]) / (double)(p->framerate[1] * 1000000));
            p->frame_time = frame->ts;
        }
        p->next_frame_requested = 0;
    } else {
        update_time(p, -1);
        int ret = nmd_get_frame(p->nmd_ctx, p->frame_time, &frame);
        if (ret < 0)
            return;
    }

    if (p->seeking) {
        reset_running_time(p);
        p->seeking = 0;
    }

    if (!frame) {
        SDL_RenderClear(p->renderer);
        if (p->texture)
            SDL_RenderCopy(p->renderer, p->texture, NULL, &dst);
        return;
    }

    if (!p->texture ||
        p->texture_width != frame->width ||
        p->texture_height != frame->height) {
        if (p->texture)
            SDL_DestroyTexture(p->texture);
        p->texture = SDL_CreateTexture(p->renderer,
                                       SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       frame->width,
                                       frame->height);
        if (!p->texture) {
            fprintf(stderr, "Failed to allocate SDL texture: %s\n", SDL_GetError());
            nmd_frame_releasep(&frame);
            return;
        }
        p->texture_width = frame->width;
        p->texture_height = frame->height;
    }

    SDL_UpdateTexture(p->texture, NULL, frame->datap[0], frame->linesizep[0]);
    SDL_RenderClear(p->renderer);
    SDL_RenderCopy(p->renderer, p->texture, NULL, &dst);

    nmd_frame_releasep(&frame);
}

static int key_callback(struct player *p, SDL_KeyboardEvent *event)
{
    const SDL_Keycode key = event->keysym.sym;
    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_q:
        return 1;
    case SDLK_SPACE:
        p->paused ^= 1;
        reset_running_time(p);
        break;
    case SDLK_f:
        p->fullscreen ^= 1;
        SDL_SetWindowFullscreen(p->window, p->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        break;
    case SDLK_LEFT:
        update_time(p, clipi64(p->frame_ts - 10 * 1000000, 0, p->duration));
        break;
    case SDLK_RIGHT:
        update_time(p, clipi64(p->frame_ts + 10 * 1000000, 0, p->duration));
        break;
    case SDLK_o:
        p->paused = 1;
        set_frame_index(p, clipi64(p->frame_index - 1, 0, p->duration_i));
        break;
    case SDLK_p:
        p->paused = 1;
        set_frame_index(p, clipi64(p->frame_index + 1, 0, p->duration_i));
        break;
    case SDLK_s:
    case SDLK_PERIOD:
        p->paused = 1;
        p->next_frame_requested = 1;
        break;
    default:
        break;
    }

    return 0;
}

static void print_usage(const char *name)
{
    fprintf(stderr, "Usage: %s <media> [-framerate 60/1]\n", name);
}

int main(int ac, char **av)
{
    int ret = 0;
    int framerate[2] = {60, 1};

    if (ac != 2 && ac != 4) {
        print_usage(av[0]);
        return -1;
    }

    if (ac == 4) {
        if (strcmp(av[2], "-framerate") || sscanf(av[3], "%d/%d", &framerate[0], &framerate[1]) != 2) {
            print_usage(av[0]);
            return -1;
        }
    }

    struct player p = {
        .nmd_ctx = nmd_create(av[1]),
    };
    if (!p.nmd_ctx) {
        ret = -1;
        goto done;
    }

    nmd_set_option(p.nmd_ctx, "sw_pix_fmt", NMD_PIXFMT_RGBA);
    nmd_set_option(p.nmd_ctx, "auto_hwaccel", 0);

    struct nmd_info info = {0};
    ret = nmd_get_info(p.nmd_ctx, &info);
    if (ret < 0) {
        ret = -1;
        goto done;
    }

    p.framerate[0]    = framerate[0];
    p.framerate[1]    = framerate[1];
    p.duration_f      = info.duration;
    p.duration        = p.duration_f * 1000000;
    p.duration_i      = llrint(p.duration_f * p.framerate[0] / (double)p.framerate[1]);
    p.width           = info.width;
    p.height          = info.height;
    p.aspect_ratio[0] = info.width;
    p.aspect_ratio[1] = info.height;
    get_viewport(p.width, p.height, p.aspect_ratio, p.viewport);
    p.clock_off       = INT64_MIN;

#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        ret = -1;
        goto done;
    }

    char title[256];
    snprintf(title, sizeof(title), "nope.media - %s", av[1]);
    p.window = SDL_CreateWindow(title,
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                p.width,
                                p.height,
                                SDL_WINDOW_RESIZABLE);
    if (!p.window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        ret = -1;
        goto done;
    }

    p.renderer = SDL_CreateRenderer(p.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p.renderer) {
        fprintf(stderr, "Failed to create SDL renderer: %s\n", SDL_GetError());
        ret = -1;
        goto done;
    }
    SDL_SetRenderDrawColor(p.renderer, 0, 0, 0, 255);

    int run = 1;
    while (run) {
        render(&p);
        SDL_RenderPresent(p.renderer);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE)
                    run = 0;
                else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    size_callback(&p, event.window.data1, event.window.data2);
                break;
            case SDL_KEYDOWN:
                run = key_callback(&p, &event.key) == 0;
                break;
            case SDL_MOUSEBUTTONDOWN:
                mouse_buttondown_callback(&p, &event.button);
                break;
            case SDL_MOUSEBUTTONUP:
                mouse_buttonup_callback(&p, &event.button);
                break;
            case SDL_MOUSEMOTION:
                mouse_pos_callback(&p, &event.motion);
                break;
            }
        }
    }

done:
    SDL_DestroyTexture(p.texture);
    SDL_DestroyRenderer(p.renderer);
    SDL_DestroyWindow(p.window);
    SDL_Quit();
    nmd_freep(&p.nmd_ctx);

    return ret;
}
