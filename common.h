//
//  common.h
//  TextAppMaker
//
//  Created by Thomas Foster on 5/26/23.
//

#ifndef common_h
#define common_h

#include <stdint.h>
#include <SDL2/SDL.h>

typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;

extern SDL_Window * window;
extern SDL_Renderer * renderer;

#endif /* common_h */
