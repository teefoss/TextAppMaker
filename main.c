//
//  main.c
//  TextAppMaker
//
//  Created by Thomas Foster on 5/26/23.
//

#include "text.h"
#include "common.h"

#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#define FONT_W 8
#define FONT_H 16
#define MAX(a, b) ((a > b) ? (a) : (b))
#define MIN(a, b) ((a < b) ? (a) : (b))
#define CLAMP(a, min, max) (a = a < min ? min : a > max ? max : a)

#define GET_FG(x) ((x & 0x0F00) >> 8)
#define GET_BG(x) ((x & 0xF000) >> 12)
#define GET_CHAR(x) (x & 0xFF)
#define SET_FG(x, fg) do { x &= 0xF0FF; x |= fg << 8; } while ( 0 );
#define SET_BG(x, bg) do { x &= 0x0FFF; x |= bg << 12; } while ( 0 );
#define SET_CHAR(x, ch) do { x &= 0xFF00; x |= ch; } while ( 0 );

#define CHAR_PAL (py * 16 + px)

#define SCALE 2.0f
#define WINDOW_W ((app_w + 16) * FONT_W)
#define WINDOW_H (MAX(16 * FONT_H, app_h * FONT_H))

const char * file_name;
SDL_Window * window;
SDL_Renderer * renderer;
SDL_Texture * texture;
u8 app_w = 80;
u8 app_h = 25;

// Current foreground and background color
u8 bg = 0;
u8 fg = 7;

// Work area cursor
int cx;
int cy;

// Palette cursor
int px;
int py;

enum {
    MODE_PAINT,
    MODE_TEXT,
    NUM_MODES,
} mode;

int last_mode;

const SDL_Color palette[16] = {
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0xAA },
//    { 0x04, 0x14, 0x41 }, // Get that funkly blue color!
    { 0x00, 0xAA, 0x00 },
    { 0x00, 0xAA, 0xAA },
    { 0xAA, 0x00, 0x00 },
    { 0xAA, 0x00, 0xAA },
    { 0xAA, 0x55, 0x00 },
    { 0xAA, 0xAA, 0xAA },
    { 0x55, 0x55, 0x55 },
    { 0x55, 0x55, 0xFF },
    { 0x55, 0xFF, 0x55 },
    { 0x55, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55 },
    { 0xFF, 0x55, 0xFF },
    { 0xFF, 0xFF, 0x55 },
    { 0xFF, 0xFF, 0xFF },
};

const SDL_Color orange = { 0xFF, 0xA5, 0x00, 0xFF };

#define MAX_WIDTH 256
#define MAX_HEIGHT 256

u16 map[MAX_HEIGHT][MAX_WIDTH];
u16 copy[MAX_HEIGHT][MAX_WIDTH];

bool dragging;
bool got_box;
int left, right, top, bottom; // location of selection box in map
int copy_left, copy_right, copy_top, copy_bottom; // location of data in copy
SDL_Point drag_start;
SDL_Point drag_end;

void SetRenderColor(SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void SetPaletteColor(int index)
{
    SDL_Color c = palette[index];
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
}

void RenderBackground(int x, int y, int index)
{
    SetPaletteColor(index);
    SDL_Rect r = { x, y, FONT_W, FONT_H };
    SDL_RenderFillRect(renderer, &r);
}

void AdvanceCursor(void)
{
    cx++;
    if ( cx >= app_w ) {
        cx = 0;
        if ( cy < app_h - 1 ) {
            cy++;
        }
    }
}

// Update SDL_Window with new app_w and app_h
void ResizeWindow(void)
{
    SDL_SetWindowSize(window, WINDOW_W * SCALE, WINDOW_H * SCALE);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_RenderSetLogicalSize(renderer, WINDOW_W, WINDOW_H);

    char buf[100] = { 0 };
    snprintf(buf, 100, "%s: %d x %d", file_name, app_w, app_h);
    SDL_SetWindowTitle(window, buf);

    if ( texture ) {
        SDL_DestroyTexture(texture);
    }

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_TARGET,
                                app_w * FONT_W,
                                app_h * FONT_H);

    SDL_SetRenderTarget(renderer, texture);

    for ( int y = 0; y < app_h; y++ ) {
        for ( int x = 0; x < app_w; x++ ) {
            int rx = x * FONT_W;
            int ry = y * FONT_H;

            RenderBackground(rx, ry, GET_BG(map[y][x]));
            SetPaletteColor(GET_FG(map[y][x]));
            PrintChar(rx, ry, GET_CHAR(map[y][x]));
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
}

/// Draw texture cell x, y from data in map[y][x]
void RefreshTexture(int x, int y)
{
    int rx = x * FONT_W; // Render position.
    int ry = y * FONT_H;

    SDL_SetRenderTarget(renderer, texture);

    s16 cell = map[y][x];

    RenderBackground(rx, ry, GET_BG(cell));
    SetPaletteColor(GET_FG(cell));
    PrintChar(rx, ry, GET_CHAR(cell));

    SDL_SetRenderTarget(renderer, NULL);
}

void UpdateMapPosition(int x, int y, u8 ch, u8 _fg, u8 _bg)
{
    SET_CHAR(map[y][x], ch);
    SET_FG(map[y][x], _fg);
    SET_BG(map[y][x], _bg);

    RefreshTexture(x, y);
}

void SaveFile(void)
{
    FILE * file = fopen(file_name, "wb");
    if ( file == NULL ) {
        printf("Failed to create '%s'!\n", file_name);
        return;
    }

    fwrite(&app_w, sizeof(app_w), 1, file);
    fwrite(&app_h, sizeof(app_h), 1, file);

    for ( int y = 0; y < app_h; y++ ) {
        for ( int x = 0; x < app_w; x++ ) {
            fwrite(&map[y][x], sizeof(map[0][0]), 1, file);
        }
    }

    fclose(file);
    printf("Saved '%s'\n", file_name);
}

void LoadFile(void)
{
    FILE * file = fopen(file_name, "rb");
    if ( file == NULL ) {
        printf("'%s' does not exist, it will be created.\n", file_name);
        return;
    }

    fread(&app_w, sizeof(app_w), 1, file);
    fread(&app_h, sizeof(app_h), 1, file);

    for ( int y = 0; y < app_h; y++ ) {
        for ( int x = 0; x < app_w; x++ ) {
            fread(&map[y][x], sizeof(map[0][0]), 1, file);
        }
    }

    fclose(file);
}

void FloodFill(int x, int y, u16 replace, u16 new)
{
    if ( x < 0 || y < 0 || x > app_w - 1 || y > app_h - 1 ) {
        return;
    }

    if ( map[y][x] != replace ) {
        return;
    }

    UpdateMapPosition(x, y, GET_CHAR(new), GET_FG(new), GET_BG(new));

    FloodFill(x + 1, y, replace, new);
    FloodFill(x - 1, y, replace, new);
    FloodFill(x, y + 1, replace, new);
    FloodFill(x, y - 1, replace, new);
}

int main(int argc, char ** argv)
{
    if ( argc != 2 ) {
        printf("Error: no file specified\n");
        printf("usage: %s [filename]\n", argv[0]);
        return -1;
    }

    file_name = argv[1];
    LoadFile();

    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              640,
                              480,
                              0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ResizeWindow();

//    SDL_ShowCursor(SDL_DISABLE);
    SDL_StartTextInput();

    bool run = true;
    while ( run ) {
        SDL_Keymod mods = SDL_GetModState();

        int mx, my;
        u32 buttons = SDL_GetMouseState(&mx, &my);
        mx /= FONT_W * SCALE;
        my /= FONT_H * SCALE;

        SDL_Event event;
        while ( SDL_PollEvent(&event) ) {
            switch ( event.type ) {

                case SDL_QUIT:
                    run = false;
                    break;

                case SDL_KEYDOWN:
                    switch ( event.key.keysym.sym ) {

                        case SDLK_c:
                            if ( //mode == MODE_COPY
                                //&&
                                (mods & KMOD_GUI)
                                && got_box )
                            {
                                for ( int y = top; y <= bottom; y++ ) {
                                    for ( int x = left; x <= right; x++ ) {
                                        copy[y][x] = map[y][x];
                                    }
                                }

                                copy_left = left;
                                copy_right = right;
                                copy_top = top;
                                copy_bottom = bottom;

                                got_box = false;
                            }
                            break;

                        case SDLK_v:
                            if ( //mode == MODE_COPY
                                //&&
                                (mods & KMOD_GUI)
                                && !got_box )
                            {
                                for ( int y = copy_top; y <= copy_bottom; y++ ) {
                                    for ( int x = copy_left; x <= copy_right; x++ ) {
                                        int map_x = mx + (x - copy_left);
                                        int map_y = my + (y - copy_top);
                                        map[map_y][map_x] = copy[y][x];
                                        RefreshTexture(map_x, map_y);
                                    }
                                }
                            }
                            break;

                        case SDLK_TAB:
                            mode = (mode + 1) % NUM_MODES;
                            got_box = false; // Cancel selection box.
                            break;

                        case SDLK_s:
                            if ( mods & KMOD_GUI ) {
                                SaveFile();
                            }
                            break;

                        case SDLK_ESCAPE:
                            got_box = false;
                            break;

                        case SDLK_f:
                            if ( mode == MODE_PAINT && mods & KMOD_GUI) {
                                u16 new = 0;
                                SET_CHAR(new, CHAR_PAL);
                                SET_FG(new, fg);
                                SET_BG(new, bg);
                                if ( map[my][mx] != new ) {
                                    FloodFill(mx, my, map[my][mx], new);
                                }
                            }
                            break;

                        case SDLK_EQUALS:
                            if ( mode != MODE_TEXT  ) {
                                if ( mods & KMOD_SHIFT ) {
                                    bg = (bg + 1) % 16;
                                } else {
                                    fg = (fg + 1) % 16;
                                }
                            }
                            break;

                        case SDLK_MINUS:
                            if ( mode != MODE_TEXT  ) {
                                if ( mods & KMOD_SHIFT ) {
                                    if ( bg == 0 )
                                        bg = 15;
                                    else
                                        bg--;
                                } else {
                                    if ( fg == 0 )
                                        fg = 15;
                                    else
                                        fg--;
                                }
                            }
                            break;

                        case SDLK_UP:
                            if ( mods & KMOD_SHIFT ) {
                                py--;
                                if ( py < 0 ) py = 15;
                            } else if ( mods & KMOD_ALT ) {
                                app_h++;
                                ResizeWindow();
                            } else {
                                cy--;
                                if ( cy < 0 ) {
                                    cy = app_h - 1;
                                }
                            }
                            break;

                        case SDLK_DOWN:
                            if ( mods & KMOD_SHIFT ) {
                                py = (py + 1) % 16;
                            } else if ( mods & KMOD_ALT ) {
                                app_h--;
                                ResizeWindow();
                            } else {
                                cy++;
                                if ( cy >= app_h ) {
                                    cy = 0;
                                }
                            }
                            break;

                        case SDLK_LEFT:
                            if ( mods & KMOD_SHIFT ) {
                                px--;
                                if ( px < 0 ) px = 15;
                            } else if ( mods & KMOD_ALT ) {
                                app_w--;
                                ResizeWindow();
                            } else {
                                cx--;
                                if ( cx < 0 ) {
                                    cx = app_w - 1;
                                }
                            }
                            break;

                        case SDLK_RIGHT:
                            if ( mods & KMOD_SHIFT ) {
                                px = (px + 1) % 16;
                            } else if ( mods & KMOD_ALT ) {
                                app_w++;
                                ResizeWindow();
                            } else {
                                cx++;
                                if ( cx >= app_w ) {
                                    cx = 0;
                                }
                            }
                            break;

                        case SDLK_BACKSPACE:
                            if ( got_box ) {
                                int bg_set;
                                if ( mods & KMOD_GUI )
                                    bg_set = 0;
                                else
                                    bg_set = bg;

                                for ( int y = top; y <= bottom; y++ ) {
                                    for ( int x = left; x <= right; x++ ) {
                                        UpdateMapPosition(x, y, 0, fg, bg_set);
                                    }
                                }
                            } else {
                                SET_CHAR(map[cy][cx], 0);
                            }
                            break;

                        default:
                            break;
                    }
                    break;

                case SDL_KEYUP:
                    switch ( event.key.keysym.sym ) {
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT:
                            dragging = false;
//                            got_box = false;
                            break;
                        default:
                            break;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    switch ( event.button.button ) {

                        case SDL_BUTTON_LEFT:

                            if ( mods & KMOD_SHIFT ) {
                                dragging = true;
                                drag_start = (SDL_Point){ mx, my };
                            }
                            break;

                        default:
                            break;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    switch ( event.button.button ) {

                        case SDL_BUTTON_LEFT:
//                            if ( mode == MODE_COPY ) {
                            if ( mods & KMOD_SHIFT ) {
                                got_box = true;
                                dragging = false;
                            }
                            break;

                        default:
                            break;
                    }
                    break;

                case SDL_TEXTINPUT:
                    if ( mode != MODE_TEXT  ) break;
                    if ( isprint(event.text.text[0]) ) {
                        UpdateMapPosition(cx, cy, event.text.text[0], fg, bg);
                        AdvanceCursor();
                    }
                    break;

                default:
                    break;
            }
        }

        if ( dragging ) {
            drag_end = (SDL_Point){ mx, my };
            // Update selection box
            if ( drag_start.x < drag_end.x ) {
                left = drag_start.x;
                right = drag_end.x;
            } else {
                left = drag_end.x;
                right = drag_start.x;
            }

            if ( drag_start.y < drag_end.y ) {
                top = drag_start.y;
                bottom = drag_end.y;
            } else {
                top = drag_end.y;
                bottom = drag_start.y;
            }
        }

        // Handle left click

        if ( buttons & SDL_BUTTON(SDL_BUTTON_LEFT) ) {
            if ( mode == MODE_PAINT && !(mods & KMOD_SHIFT) ) {
                if ( mx >= 0 && mx < app_w && my >= 0 && my < app_h ) {
                    UpdateMapPosition(mx, my, CHAR_PAL, fg, bg);
                } else if ( mx >= app_w
                           && mx < app_w + 16
                           && my >= 0
                           && my < 16 )
                {

                    px = mx - app_w;
                    py = my;
                }
            } else if ( mode == MODE_TEXT ) {
                cx = mx;
                cy = my;
            }
        }

        // Pick up what's under cursor

        if ( buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)
            && (mode == MODE_PAINT || mode == MODE_TEXT) ) {
            fg = GET_FG(map[my][mx]);
            bg = GET_BG(map[my][mx]);
            int ch = GET_CHAR(map[my][mx]);
            px = ch % 16;
            py = ch / 16;
        }

        //
        // Render
        //

        SDL_SetRenderDrawColor(renderer, 16, 16, 16, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
        for ( int x = FONT_W; x < WINDOW_W + WINDOW_H; x += FONT_W ) {
            SDL_RenderDrawLine(renderer, x, 0, 0, x * 2);
        }

        // Render `map` texture

        SDL_Rect map_rect = { 0, 0, FONT_W * app_w, FONT_H * app_h };
        SDL_RenderCopy(renderer, texture, NULL, &map_rect);

        // Render Character Palette

        if ( mode == MODE_PAINT ) {
            for ( int y = 0; y < 16; y++ ) {
                for ( int x = 0; x < 16; x++ ) {
                    int rx = (x + app_w) * FONT_W;
                    int ry = y * FONT_H;
                    RenderBackground(rx, ry, bg);
                    SetPaletteColor(fg);
                    PrintChar(rx, ry, y * 16 + x);
                }
            }

            if ( dragging || got_box ) {
                SDL_Rect selection = {
                    left * FONT_W,
                    top * FONT_H,
                    ((right - left) + 1) * FONT_W,
                    ((bottom - top) + 1) * FONT_H,
                };

                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
                SDL_RenderFillRect(renderer, &selection);

                SetRenderColor(orange);
                SDL_RenderDrawRect(renderer, &selection);
            }

        } else if ( mode == MODE_TEXT ) {
            SetPaletteColor(fg);
            PrintString(app_w * FONT_W + 2, 2, "Text Entry Mode");
        }
//        else if ( mode == MODE_COPY ) {
//            SetPaletteColor(15);
//            PrintString(app_w * FONT_W + 2, 2, "Copy Mode");

//        }

        // Render Cursors

        if ( SDL_GetTicks() % 600 < 300 ) {
            if ( mode == MODE_TEXT ) {
                PrintChar(cx * FONT_W, cy * FONT_H, 219);
            } else if ( mode == MODE_PAINT ) {
                PrintChar((app_w + px) * FONT_W, py * FONT_H, 219);
            }
        }

        // Render Mouse Cursor

        SDL_Rect mouse_rect = { mx * FONT_W, my * FONT_H, FONT_W, FONT_H };
        SDL_SetRenderDrawColor(renderer, 0xFF, 0xA5, 0x00, 0xff);
        SDL_RenderDrawRect(renderer, &mouse_rect);
        if ( mode == MODE_PAINT ) {
            if ( mx >= 0 && my >= 0 && mx < app_w && my < app_h ) {
                SetPaletteColor(fg);
                PrintChar(mx * FONT_W, my * FONT_H, CHAR_PAL);
            }
        }

        // Render Workarea / Character Palette dividers

        SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
        int divider_x = app_w * FONT_W - 1;
        SDL_RenderDrawLine(renderer, divider_x, 0, divider_x, WINDOW_H);
        int divider_y = FONT_H * 16;
        SDL_RenderDrawLine(renderer, app_w * FONT_W, divider_y, WINDOW_W, divider_y);

        // Render Cursor Position
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        PrintString(app_w * FONT_W, 16 * FONT_H, "%d, %d", mx, my);

        SDL_RenderPresent(renderer);
        SDL_Delay(15);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
