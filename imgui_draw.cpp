// dear imgui, v1.50 WIP
// (drawing code)

// Contains implementation for
// - ImDrawList
// - ImDrawData

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_PLACEMENT_NEW
#include "imgui_internal.h"

#include <stdio.h> // vsnprintf, sscanf, printf
#if !defined(alloca)
#ifdef _WIN32
#include <malloc.h> // alloca
#elif(defined(__FreeBSD__) || defined(FreeBSD_kernel) || defined(__DragonFly__)) && !defined(__GLIBC__)
#include <stdlib.h> // alloca. FreeBSD uses stdlib.h unless GLIBC
#else
#include <alloca.h> // alloca
#endif
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4505) // unreferenced local function has been removed (stb stuff)
#pragma warning(disable : 4996) // 'This function or variable may be unsafe': strcpy, strdup, sprintf, vsnprintf, sscanf, fopen
#define snprintf _snprintf
#endif

#ifdef __clang__
#pragma clang diagnostic ignored "-Wold-style-cast"      // warning : use of old-style cast                              // yes, they are more terse.
#pragma clang diagnostic ignored "-Wfloat-equal"         // warning : comparing floating point with == or != is unsafe   // storing and comparing against same constants ok.
#pragma clang diagnostic ignored "-Wglobal-constructors" // warning : declaration requires a global destructor           // similar to above, not sure what the exact difference it.
#pragma clang diagnostic ignored "-Wsign-conversion"     // warning : implicit conversion changes signedness             //
#pragma clang diagnostic ignored "-Wreserved-id-macro"   // warning : macro name is a reserved identifier                //
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"  // warning: 'xxxx' defined but not used
#pragma GCC diagnostic ignored "-Wdouble-promotion" // warning: implicit conversion from 'float' to 'double' when passing argument to function
#pragma GCC diagnostic ignored "-Wconversion"       // warning: conversion to 'xxxx' from 'xxxx' may alter its value
#endif

//-----------------------------------------------------------------------------
// ImDrawList
//-----------------------------------------------------------------------------

static const ImVec4 GNullClipRect(-8192.0f, -8192.0f, +8192.0f, +8192.0f); // Large values that are easy to encode in a few bits+shift

void ImDrawList::Clear() {
    CmdBuffer.resize(0);
    IdxBuffer.resize(0);
    VtxBuffer.resize(0);
    _VtxCurrentIdx = 0;
    _VtxWritePtr   = NULL;
    _IdxWritePtr   = NULL;
    _ClipRectStack.resize(0);
    _Path.resize(0);
    _ChannelsCurrent = 0;
    _ChannelsCount   = 1;
    // NB: Do not clear channels so our allocations are re-used after the first frame.
}

void ImDrawList::ClearFreeMemory() {
    CmdBuffer.clear();
    IdxBuffer.clear();
    VtxBuffer.clear();
    _VtxCurrentIdx = 0;
    _VtxWritePtr   = NULL;
    _IdxWritePtr   = NULL;
    _ClipRectStack.clear();
    _Path.clear();
    _ChannelsCurrent = 0;
    _ChannelsCount   = 1;
    for(int i = 0; i < _Channels.Size; i++) {
        if(i == 0)
            memset(&_Channels[0], 0, sizeof(_Channels[0])); // channel 0 is a copy of CmdBuffer/IdxBuffer, don't destruct again
        _Channels[i].CmdBuffer.clear();
        _Channels[i].IdxBuffer.clear();
    }
    _Channels.clear();
}

// Use macros because C++ is a terrible language, we want guaranteed inline, no code in header, and no overhead in Debug mode
#define GetCurrentClipRect() (_ClipRectStack.Size ? _ClipRectStack.Data[_ClipRectStack.Size - 1] : GNullClipRect)

void ImDrawList::AddDrawCmd() {
    ImDrawCmd draw_cmd;
    draw_cmd.ClipRect = GetCurrentClipRect();

    IM_ASSERT(draw_cmd.ClipRect.x <= draw_cmd.ClipRect.z && draw_cmd.ClipRect.y <= draw_cmd.ClipRect.w);
    CmdBuffer.push_back(draw_cmd);
}

void ImDrawList::AddCallback(ImDrawCallback callback, void *callback_data) {
    ImDrawCmd *current_cmd = CmdBuffer.Size ? &CmdBuffer.back() : NULL;
    if(!current_cmd || current_cmd->ElemCount != 0 || current_cmd->UserCallback != NULL) {
        AddDrawCmd();
        current_cmd = &CmdBuffer.back();
    }
    current_cmd->UserCallback     = callback;
    current_cmd->UserCallbackData = callback_data;

    AddDrawCmd(); // Force a new command after us (see comment below)
}

// Our scheme may appears a bit unusual, basically we want the most-common calls AddLine AddRect etc. to not have to perform any check so we always have a command ready in the stack.
// The cost of figuring out if a new command has to be added or if we can merge is paid in those Update** functions only.
void ImDrawList::UpdateClipRect() {
    // If current command is used with different settings we need to add a new command
    const ImVec4 curr_clip_rect = GetCurrentClipRect();
    ImDrawCmd *  curr_cmd       = CmdBuffer.Size > 0 ? &CmdBuffer.Data[CmdBuffer.Size - 1] : NULL;
    if(!curr_cmd || (curr_cmd->ElemCount != 0 && memcmp(&curr_cmd->ClipRect, &curr_clip_rect, sizeof(ImVec4)) != 0) || curr_cmd->UserCallback != NULL) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    ImDrawCmd *prev_cmd = CmdBuffer.Size > 1 ? curr_cmd - 1 : NULL;
    if(curr_cmd->ElemCount == 0 && prev_cmd && memcmp(&prev_cmd->ClipRect, &curr_clip_rect, sizeof(ImVec4)) == 0 && prev_cmd->UserCallback == NULL)
        CmdBuffer.pop_back();
    else
        curr_cmd->ClipRect = curr_clip_rect;
}

// Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level ImGui::PushClipRect() to affect logic (hit-testing and widget culling)
void ImDrawList::PushClipRect(ImVec2 cr_min, ImVec2 cr_max, bool intersect_with_current_clip_rect) {
    ImVec4 cr(cr_min.x, cr_min.y, cr_max.x, cr_max.y);
    if(intersect_with_current_clip_rect && _ClipRectStack.Size) {
        ImVec4 current = _ClipRectStack.Data[_ClipRectStack.Size - 1];
        if(cr.x < current.x)
            cr.x = current.x;
        if(cr.y < current.y)
            cr.y = current.y;
        if(cr.z > current.z)
            cr.z = current.z;
        if(cr.w > current.w)
            cr.w = current.w;
    }
    cr.z = ImMax(cr.x, cr.z);
    cr.w = ImMax(cr.y, cr.w);

    _ClipRectStack.push_back(cr);
    UpdateClipRect();
}

void ImDrawList::PushClipRectFullScreen() {
    PushClipRect(ImVec2(GNullClipRect.x, GNullClipRect.y), ImVec2(GNullClipRect.z, GNullClipRect.w));
    //PushClipRect(GetVisibleRect());   // FIXME-OPT: This would be more correct but we're not supposed to access ImGuiContext from here?
}

void ImDrawList::PopClipRect() {
    IM_ASSERT(_ClipRectStack.Size > 0);
    _ClipRectStack.pop_back();
    UpdateClipRect();
}

void ImDrawList::ChannelsSplit(int channels_count) {
    IM_ASSERT(_ChannelsCurrent == 0 && _ChannelsCount == 1);
    int old_channels_count = _Channels.Size;
    if(old_channels_count < channels_count)
        _Channels.resize(channels_count);
    _ChannelsCount = channels_count;

    // _Channels[] (24 bytes each) hold storage that we'll swap with this->_CmdBuffer/_IdxBuffer
    // The content of _Channels[0] at this point doesn't matter. We clear it to make state tidy in a debugger but we don't strictly need to.
    // When we switch to the next channel, we'll copy _CmdBuffer/_IdxBuffer into _Channels[0] and then _Channels[1] into _CmdBuffer/_IdxBuffer
    memset(&_Channels[0], 0, sizeof(ImDrawChannel));
    for(int i = 1; i < channels_count; i++) {
        if(i >= old_channels_count) {
            IM_PLACEMENT_NEW(&_Channels[i])
            ImDrawChannel();
        } else {
            _Channels[i].CmdBuffer.resize(0);
            _Channels[i].IdxBuffer.resize(0);
        }
        if(_Channels[i].CmdBuffer.Size == 0) {
            ImDrawCmd draw_cmd;
            draw_cmd.ClipRect = _ClipRectStack.back();
            _Channels[i].CmdBuffer.push_back(draw_cmd);
        }
    }
}

void ImDrawList::ChannelsMerge() {
    // Note that we never use or rely on channels.Size because it is merely a buffer that we never shrink back to 0 to keep all sub-buffers ready for use.
    if(_ChannelsCount <= 1)
        return;

    ChannelsSetCurrent(0);
    if(CmdBuffer.Size && CmdBuffer.back().ElemCount == 0)
        CmdBuffer.pop_back();

    int new_cmd_buffer_count = 0, new_idx_buffer_count = 0;
    for(int i = 1; i < _ChannelsCount; i++) {
        ImDrawChannel &ch = _Channels[i];
        if(ch.CmdBuffer.Size && ch.CmdBuffer.back().ElemCount == 0)
            ch.CmdBuffer.pop_back();
        new_cmd_buffer_count += ch.CmdBuffer.Size;
        new_idx_buffer_count += ch.IdxBuffer.Size;
    }
    CmdBuffer.resize(CmdBuffer.Size + new_cmd_buffer_count);
    IdxBuffer.resize(IdxBuffer.Size + new_idx_buffer_count);

    ImDrawCmd *cmd_write = CmdBuffer.Data + CmdBuffer.Size - new_cmd_buffer_count;
    _IdxWritePtr         = IdxBuffer.Data + IdxBuffer.Size - new_idx_buffer_count;
    for(int i = 1; i < _ChannelsCount; i++) {
        ImDrawChannel &ch = _Channels[i];
        if(int sz = ch.CmdBuffer.Size) {
            memcpy(cmd_write, ch.CmdBuffer.Data, sz * sizeof(ImDrawCmd));
            cmd_write += sz;
        }
        if(int sz = ch.IdxBuffer.Size) {
            memcpy(_IdxWritePtr, ch.IdxBuffer.Data, sz * sizeof(ImDrawIdx));
            _IdxWritePtr += sz;
        }
    }
    AddDrawCmd();
    _ChannelsCount = 1;
}

void ImDrawList::ChannelsSetCurrent(int idx) {
    IM_ASSERT(idx < _ChannelsCount);
    if(_ChannelsCurrent == idx)
        return;
    memcpy(&_Channels.Data[_ChannelsCurrent].CmdBuffer, &CmdBuffer, sizeof(CmdBuffer)); // copy 12 bytes, four times
    memcpy(&_Channels.Data[_ChannelsCurrent].IdxBuffer, &IdxBuffer, sizeof(IdxBuffer));
    _ChannelsCurrent = idx;
    memcpy(&CmdBuffer, &_Channels.Data[_ChannelsCurrent].CmdBuffer, sizeof(CmdBuffer));
    memcpy(&IdxBuffer, &_Channels.Data[_ChannelsCurrent].IdxBuffer, sizeof(IdxBuffer));
    _IdxWritePtr = IdxBuffer.Data + IdxBuffer.Size;
}

// NB: this can be called with negative count for removing primitives (as long as the result does not underflow)
void ImDrawList::PrimReserve(int idx_count, int vtx_count) {
    ImDrawCmd &draw_cmd = CmdBuffer.Data[CmdBuffer.Size - 1];
    draw_cmd.ElemCount += idx_count;

    int vtx_buffer_size = VtxBuffer.Size;
    VtxBuffer.resize(vtx_buffer_size + vtx_count);
    _VtxWritePtr = VtxBuffer.Data + vtx_buffer_size;

    int idx_buffer_size = IdxBuffer.Size;
    IdxBuffer.resize(idx_buffer_size + idx_count);
    _IdxWritePtr = IdxBuffer.Data + idx_buffer_size;
}

void ImDrawList::AddConvexPolyFilled(const ImVec2 *points, const int points_count, const ImColor &col, bool anti_aliased) {
}

void ImDrawList::PathRect(const ImVec2 &a, const ImVec2 &b, float rounding, int rounding_corners) {
    PathLineTo(a);
    PathLineTo(ImVec2(b.x, a.y));
    PathLineTo(b);
    PathLineTo(ImVec2(a.x, b.y));
}

void ImDrawList::AddLine(const ImVec2 &a, const ImVec2 &b, const ImColor &col, int ch) {

    if(a.x == b.x) {
        // vertical line
        int start = ImMin(a.y, b.y);
        int end   = ImMax(a.y, b.y);
        int cnt   = end - start;

        PrimReserve(cnt, cnt);
        for(int y = start; y < end; y++) {
            PrimChar(ImVec2(a.x, y), ch, col);
        }
    } else if(a.y == b.y) {
        // horizontal line
        int start = ImMin(a.x, b.x);
        int end   = ImMax(a.x, b.x);
        int cnt   = end - start;
        PrimReserve(cnt, cnt);

        for(int x = start; x < end; x++) {
            PrimChar(ImVec2(x, a.y), ch, col);
        }
    } else {
        float deltax = b.x - a.x;
        float deltay = b.y - a.y;
        float slope  = deltay / (float)deltax;

        auto abs = [](int a) -> int { return a >= 0 ? a : -a; };

        // Bresenham for the win!
        int x0 = a.x + 0.5f;
        int x1 = b.x + 0.5f;
        int y0 = a.y + 0.5f;
        int y1 = b.y + 0.5f;
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = (dx > dy ? dx : -dy) / 2, e2;


        int c = L'.';
		int secondChar = 0;
        for(;;) {
            AddChar(ImVec2(x0, y0), col, c);
            if(x0 == x1 && y0 == y1)
                break;
			int movex = 0;
            e2 = err;
            if(e2 > -dx) {
                err -= dy;
                x0 += sx;
				movex = sx;
            }
			int movey = 0;
            if(e2 < dy) {
                err += dx;
                y0 += sy;
				movey = sy;
            }

			if(movex == 0){
        		c = L'│';
				c = L'|';
			}else{
				if(movey == 0){
        			c = L'─';
				}
				else if(movex * movey > 0){
					c = L'\\';
				}else{
					c = L'/';
				}
			}
			if(secondChar == 0){
				secondChar = c;
			}
        }
		if(secondChar == 0){
			secondChar = L'─';
		}
		AddChar(ImVec2(a.x + 0.5f, a.y + 0.5f), col, secondChar);
    }
}

void ImDrawList::AddChar(const ImVec2 &pos, const ImColor &col, int ch) {
    const ImVec4 curr_clip_rect = _ClipRectStack.back();
    if(pos.x < curr_clip_rect.x ||
       pos.y < curr_clip_rect.y ||
       pos.x >= curr_clip_rect.z ||
       pos.y >= curr_clip_rect.w) {
        return;
    }

    PrimReserve(1, 1);
    PrimChar(pos, ch, col);
}

void ImDrawList::AddRectFilled(const ImVec2 &a, const ImVec2 &b, const ImColor &col, float rounding, int rounding_corners) {

    for(int y = a.y + 1; y < b.y; y++) {
        for(int x = a.x + 1; x < b.x; x++) {
            AddChar(ImVec2(x, y), col, ' ');
        }
    }

    const ImVec4 curr_clip_rect = GetCurrentClipRect();

    PrimReserve(4, 4);
    PrimChar(ImVec2(a.x, a.y), L'╭', col);
    PrimChar(ImVec2(a.x, b.y), L'╰', col);
    PrimChar(ImVec2(b.x, b.y), L'╯', col);
    PrimChar(ImVec2(b.x, a.y), L'╮', col);

    for(int x = ImMax(int(a.x + 1), int(curr_clip_rect.x)); x < ImMin(int(b.x), int(curr_clip_rect.z + 0.5)); x++) {

        PrimReserve(2, 2);
        PrimChar(ImVec2(x, a.y), L'─', col);
        PrimChar(ImVec2(x, b.y), L'─', col);
    }
    for(int y = ImMax(a.y + 1, curr_clip_rect.y); y < ImMin(b.y, curr_clip_rect.w); y++) {
        PrimReserve(2, 2);
        PrimChar(ImVec2(a.x, y), L'│', col);
        PrimChar(ImVec2(b.x, y), L'│', col);
    }
}

void ImDrawList::AddQuad(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImVec2 &d, const ImColor &col, float thickness) {
    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
}

void ImDrawList::AddQuadFilled(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImVec2 &d, const ImColor &col) {
    int minx = ImMin(ImMin(a.x, b.x), ImMin(c.x, d.x));
    int miny = ImMin(ImMin(a.y, b.y), ImMin(c.y, d.y));
    int maxx = ImMax(ImMax(a.x, b.x), ImMax(c.x, d.x));
    int maxy = ImMax(ImMax(a.y, b.y), ImMax(c.y, d.y));

    for(int y = miny; y < maxy; ++y) {
        for(int x = minx; x < maxx; ++x) {
            AddChar(ImVec2(x, y), col, ' ');
        }
    }
}

void ImDrawList::AddTriangle(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImColor &col, float thickness) {
    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
}

void ImDrawList::AddTriangleFilled(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c, const ImColor &col) {
    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathFill(col);
}

void ImDrawList::AddCircle(const ImVec2 &centre, float radius, const ImColor &col, int num_segments, float thickness) {
    // TODO(jon)
}

void ImDrawList::AddCircleFilled(const ImVec2 &centre, float radius, const ImColor &col, int num_segments) {
    // TODO(jon)
}

void ImDrawList::AddTextEx(const ImVec2 &pos, const ImColor &col, const char *text_begin, const char *text_end, float wrap_width, const ImVec4 *cpu_fine_clip_rect) {
    if(text_end == NULL)
        text_end = text_begin + strlen(text_begin);
    if(text_begin == text_end)
        return;

    ImVec4 clip_rect = _ClipRectStack.back();
    if(cpu_fine_clip_rect) {
        clip_rect.x = ImMax(clip_rect.x, cpu_fine_clip_rect->x);
        clip_rect.y = ImMax(clip_rect.y, cpu_fine_clip_rect->y);
        clip_rect.z = ImMin(clip_rect.z, cpu_fine_clip_rect->z);
        clip_rect.w = ImMin(clip_rect.w, cpu_fine_clip_rect->w);
    }
    //font->RenderText(this, font_size, pos, col, clip_rect, text_begin, text_end, wrap_width, cpu_fine_clip_rect != NULL);
    // TODO (jon): render to termbox here
    int x = (int)pos.x;
    int y = (int)pos.y;

    while(text_begin != text_end) {
        AddChar(ImVec2(x++, y), col, *text_begin++);
    }
}

void ImDrawList::AddText(const ImVec2 &pos, const ImColor &col, const char *text_begin, const char *text_end) {
    AddTextEx(pos, col, text_begin, text_end);
}

#undef GetCurrentClipRect

const char *CalcWordWrapPositionA(float scale, const char *text, const char *text_end, float wrap_width) {
    // Simple word-wrapping for English, not full-featured. Please submit failing cases!
    // FIXME: Much possible improvements (don't cut things like "word !", "word!!!" but cut within "word,,,,", more sensible support for punctuations, support for Unicode punctuations, etc.)

    // For references, possible wrap point marked with ^
    //  "aaa bbb, ccc,ddd. eee   fff. ggg!"
    //      ^    ^    ^   ^   ^__    ^    ^

    // List of hardcoded separators: .,;!?'"

    // Skip extra blanks after a line returns (that includes not counting them in width computation)
    // e.g. "Hello    world" --> "Hello" "World"

    // Cut words that cannot possibly fit within one line.
    // e.g.: "The tropical fish" with ~5 characters worth of width --> "The tr" "opical" "fish"

    float line_width  = 0.0f;
    float word_width  = 0.0f;
    float blank_width = 0.0f;

    const char *word_end      = text;
    const char *prev_word_end = NULL;
    bool        inside_word   = true;

    const char *s = text;
    while(s < text_end) {
        unsigned int c = (unsigned int)*s;
        const char * next_s;
        if(c < 0x80)
            next_s = s + 1;
        else
            next_s = s + ImTextCharFromUtf8(&c, s, text_end);
        if(c == 0)
            break;

        if(c < 32) {
            if(c == '\n') {
                line_width = word_width = blank_width = 0.0f;
                inside_word                           = true;
                s                                     = next_s;
                continue;
            }
            if(c == '\r') {
                s = next_s;
                continue;
            }
        }

        const float char_width = 1.f; //((int)c < IndexXAdvance.Size ? IndexXAdvance[(int)c] : FallbackXAdvance) * scale;
        if(ImCharIsSpace(c)) {
            if(inside_word) {
                line_width += blank_width;
                blank_width = 0.0f;
            }
            blank_width += char_width;
            inside_word = false;
        } else {
            word_width += char_width;
            if(inside_word) {
                word_end = next_s;
            } else {
                prev_word_end = word_end;
                line_width += word_width + blank_width;
                word_width = blank_width = 0.0f;
            }

            // Allow wrapping after punctuation.
            inside_word = !(c == '.' || c == ',' || c == ';' || c == '!' || c == '?' || c == '\"');
        }

        // We ignore blank width at the end of the line (they can be skipped)
        if(line_width + word_width >= wrap_width) {
            // Words that cannot possibly fit within an entire line will be cut anywhere.
            if(word_width < wrap_width)
                s = prev_word_end ? prev_word_end : word_end;
            break;
        }

        s = next_s;
    }

    return s;
}

ImVec2 TrCalcTextSizeA(float size, float max_width, float wrap_width, const char *text_begin, const char *text_end, const char **remaining) {
    if(!text_end)
        text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.

    const float line_height = size;
    const float scale       = 1.f; //size / FontSize;

    ImVec2 text_size  = ImVec2(0, 0);
    float  line_width = 0.0f;

    const bool  word_wrap_enabled = (wrap_width > 0.0f);
    const char *word_wrap_eol     = NULL;

    const char *s = text_begin;
    while(s < text_end) {
        if(word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
            if(!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, wrap_width - line_width);
                if(word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
                    word_wrap_eol++;   // +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
            }

            if(s >= word_wrap_eol) {
                if(text_size.x < line_width)
                    text_size.x = line_width;
                text_size.y += line_height;
                line_width    = 0.0f;
                word_wrap_eol = NULL;

                // Wrapping skips upcoming blanks
                while(s < text_end) {
                    const char c = *s;
                    if(ImCharIsSpace(c)) {
                        s++;
                    } else if(c == '\n') {
                        s++;
                        break;
                    } else {
                        break;
                    }
                }
                continue;
            }
        }

        // Decode and advance source
        const char * prev_s = s;
        unsigned int c      = (unsigned int)*s;
        if(c < 0x80) {
            s += 1;
        } else {
            s += ImTextCharFromUtf8(&c, s, text_end);
            if(c == 0)
                break;
        }

        if(c < 32) {
            if(c == '\n') {
                text_size.x = ImMax(text_size.x, line_width);
                text_size.y += line_height;
                line_width = 0.0f;
                continue;
            }
            if(c == '\r')
                continue;
        }

        const float char_width = 1.f; //((int)c < IndexXAdvance.Size ? IndexXAdvance[(int)c] : FallbackXAdvance) * scale;
        if(line_width + char_width >= max_width) {
            s = prev_s;
            break;
        }

        line_width += char_width;
    }

    if(text_size.x < line_width)
        text_size.x = line_width;

    if(line_width > 0 || text_size.y == 0.0f)
        text_size.y += line_height;

    if(remaining)
        *remaining = s;

    return text_size;
}

//-----------------------------------------------------------------------------
// DEFAULT FONT DATA
//-----------------------------------------------------------------------------
// Compressed with stb_compress() then converted to a C array.
// Use the program in extra_fonts/binary_to_compressed_c.cpp to create the array from a TTF file.
// Decompression from stb.h (public domain) by Sean Barrett https://github.com/nothings/stb/blob/master/stb.h
//-----------------------------------------------------------------------------

static unsigned int stb_decompress_length(unsigned char *input) {
    return (input[8] << 24) + (input[9] << 16) + (input[10] << 8) + input[11];
}

static unsigned char *stb__barrier, *stb__barrier2, *stb__barrier3, *stb__barrier4;
static unsigned char *stb__dout;
static void stb__match(unsigned char *data, unsigned int length) {
    // INVERSE of memmove... write each byte before copying the next...
    IM_ASSERT(stb__dout + length <= stb__barrier);
    if(stb__dout + length > stb__barrier) {
        stb__dout += length;
        return;
    }
    if(data < stb__barrier4) {
        stb__dout = stb__barrier + 1;
        return;
    }
    while(length--)
        *stb__dout++ = *data++;
}

static void stb__lit(unsigned char *data, unsigned int length) {
    IM_ASSERT(stb__dout + length <= stb__barrier);
    if(stb__dout + length > stb__barrier) {
        stb__dout += length;
        return;
    }
    if(data < stb__barrier2) {
        stb__dout = stb__barrier + 1;
        return;
    }
    memcpy(stb__dout, data, length);
    stb__dout += length;
}

#define stb__in2(x) ((i[x] << 8) + i[(x) + 1])
#define stb__in3(x) ((i[x] << 16) + stb__in2((x) + 1))
#define stb__in4(x) ((i[x] << 24) + stb__in3((x) + 1))

static unsigned char *stb_decompress_token(unsigned char *i) {
    if(*i >= 0x20) { // use fewer if's for cases that expand small
        if(*i >= 0x80)
            stb__match(stb__dout - i[1] - 1, i[0] - 0x80 + 1), i += 2;
        else if(*i >= 0x40)
            stb__match(stb__dout - (stb__in2(0) - 0x4000 + 1), i[2] + 1), i += 3;
        else /* *i >= 0x20 */
            stb__lit(i + 1, i[0] - 0x20 + 1), i += 1 + (i[0] - 0x20 + 1);
    } else { // more ifs for cases that expand large, since overhead is amortized
        if(*i >= 0x18)
            stb__match(stb__dout - (stb__in3(0) - 0x180000 + 1), i[3] + 1), i += 4;
        else if(*i >= 0x10)
            stb__match(stb__dout - (stb__in3(0) - 0x100000 + 1), stb__in2(3) + 1), i += 5;
        else if(*i >= 0x08)
            stb__lit(i + 2, stb__in2(0) - 0x0800 + 1), i += 2 + (stb__in2(0) - 0x0800 + 1);
        else if(*i == 0x07)
            stb__lit(i + 3, stb__in2(1) + 1), i += 3 + (stb__in2(1) + 1);
        else if(*i == 0x06)
            stb__match(stb__dout - (stb__in3(1) + 1), i[4] + 1), i += 5;
        else if(*i == 0x04)
            stb__match(stb__dout - (stb__in3(1) + 1), stb__in2(4) + 1), i += 6;
    }
    return i;
}

static unsigned int stb_adler32(unsigned int adler32, unsigned char *buffer, unsigned int buflen) {
    const unsigned long ADLER_MOD = 65521;
    unsigned long       s1 = adler32 & 0xffff, s2 = adler32 >> 16;
    unsigned long       blocklen, i;

    blocklen = buflen % 5552;
    while(buflen) {
        for(i = 0; i + 7 < blocklen; i += 8) {
            s1 += buffer[0], s2 += s1;
            s1 += buffer[1], s2 += s1;
            s1 += buffer[2], s2 += s1;
            s1 += buffer[3], s2 += s1;
            s1 += buffer[4], s2 += s1;
            s1 += buffer[5], s2 += s1;
            s1 += buffer[6], s2 += s1;
            s1 += buffer[7], s2 += s1;

            buffer += 8;
        }

        for(; i < blocklen; ++i)
            s1 += *buffer++, s2 += s1;

        s1 %= ADLER_MOD, s2 %= ADLER_MOD;
        buflen -= blocklen;
        blocklen = 5552;
    }
    return (unsigned int)(s2 << 16) + (unsigned int)s1;
}

static unsigned int stb_decompress(unsigned char *output, unsigned char *i, unsigned int length) {
    unsigned int olen;
    if(stb__in4(0) != 0x57bC0000)
        return 0;
    if(stb__in4(4) != 0)
        return 0; // error! stream is > 4GB
    olen          = stb_decompress_length(i);
    stb__barrier2 = i;
    stb__barrier3 = i + length;
    stb__barrier  = output + olen;
    stb__barrier4 = output;
    i += 16;

    stb__dout = output;
    for(;;) {
        unsigned char *old_i = i;
        i                    = stb_decompress_token(i);
        if(i == old_i) {
            if(*i == 0x05 && i[1] == 0xfa) {
                IM_ASSERT(stb__dout == output + olen);
                if(stb__dout != output + olen)
                    return 0;
                if(stb_adler32(1, output, olen) != (unsigned int)stb__in4(2))
                    return 0;
                return olen;
            } else {
                IM_ASSERT(0); /* NOTREACHED */
                return 0;
            }
        }
        IM_ASSERT(stb__dout <= output + olen);
        if(stb__dout > output + olen)
            return 0;
    }
}
