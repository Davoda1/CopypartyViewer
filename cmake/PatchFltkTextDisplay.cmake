# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
# Keep oversized Source lines intact while bounding native font requests.
# This modifies only the extracted FLTK 1.4.5 build source.
if(NOT DEFINED CPV_FLTK_SOURCE_DIR)
  message(FATAL_ERROR "CPV_FLTK_SOURCE_DIR is required")
endif()
set(source_path "${CPV_FLTK_SOURCE_DIR}/src/Fl_Text_Display.cxx")
file(READ "${source_path}" source)
string(FIND "${source}" "CPV_TEXT_DISPLAY_LONG_LINES_210_R3" applied)
if(NOT applied EQUAL -1)
  return()
endif()
string(FIND "${source}" "CPV_TEXT_DISPLAY_LONG_LINES_210" old_applied)
if(NOT old_applied EQUAL -1)
  message(FATAL_ERROR "Refresh the extracted Fl_Text_Display.cxx before applying the updated Source drawing patch")
endif()

function(replace_once old new)
  string(FIND "${source}" "${old}" first)
  if(first EQUAL -1)
    message(FATAL_ERROR "Pinned FLTK Source drawing patch context is missing")
  endif()
  string(LENGTH "${old}" old_length)
  math(EXPR after "${first} + ${old_length}")
  string(SUBSTRING "${source}" ${after} -1 tail)
  string(FIND "${tail}" "${old}" duplicate)
  if(NOT duplicate EQUAL -1)
    message(FATAL_ERROR "Pinned FLTK Source drawing patch context is ambiguous")
  endif()
  string(REPLACE "${old}" "${new}" source "${source}")
  set(source "${source}" PARENT_SCOPE)
endfunction()

replace_once("static int countlines( const char *string );\n" [=[static int countlines( const char *string );

// CPV_TEXT_DISPLAY_LONG_LINES_210_R3: Xft advances have a signed 16-bit field,
// and server clipping does not limit a glyph request's length. Bound both
// measurement and drawing; never modify the text or style buffers.
static int cpv_text_batch_limit() {
  const double physical_height = fl_height() * fl_graphics_driver->scale();
  int limit = (int)(8192.0 / (physical_height > 1.0 ? physical_height : 1.0));
  if (limit > 1024) limit = 1024;
  if (limit < 4) limit = 4;
  return limit;
}

static bool cpv_text_continuation(unsigned u, unsigned previous, int regional_count) {
  return previous == 0x200d || u == 0x200d ||
         (u >= 0x300 && fl_nonspacing(u)) ||
         (u >= 0xfe00 && u <= 0xfe0f) ||
         (u >= 0xe0100 && u <= 0xe01ef) ||
         (u >= 0x1f3fb && u <= 0x1f3ff) ||
         (u >= 0xe0020 && u <= 0xe007f) ||
         (u >= 0x1f1e6 && u <= 0x1f1ff && regional_count % 2);
}

static int cpv_text_batch_length(const char *text, int length, int limit) {
  if (length <= limit) return length;
  int at = 0, last_start = 0, regional_count = 0;
  unsigned previous = 0;
  while (at < length) {
    int bytes = 1;
    const unsigned u = (unsigned char)text[at] < 128 ? (unsigned char)text[at] :
                       fl_utf8decode(text + at, text + length, &bytes);
    if (bytes < 1) bytes = 1;
    const bool continuation = at && cpv_text_continuation(u, previous, regional_count);
    if (at + bytes > limit) {
      // Keep ordinary composed characters together. A pathological cluster
      // may exceed the hard request limit; split it only at UTF-8 boundaries.
      return continuation && last_start > 0 ? last_start : at;
    }
    if (!continuation) last_start = at;
    regional_count = u >= 0x1f1e6 && u <= 0x1f1ff ? regional_count + 1 : 0;
    previous = u;
    at += bytes;
  }
  return at;
}

static int cpv_text_cluster_length(const char *text, int length) {
  // Match FLTK's existing cursor boundaries, independently of the broader
  // composed groups kept together for drawing. Bound its tag/ZWJ lookahead.
  int at = fl_utf8len(*text);
  if (at < 1) return 1;
  if (at >= length) return length;
  unsigned u;
  if (at >= 4) {
    u = fl_utf8decode(text, text + length, NULL);
    if (u >= 0x1f1e6 && u <= 0x1f1ff && 2 * at <= length) {
      u = fl_utf8decode(text + at, text + length, NULL);
      if (u >= 0x1f1e6 && u <= 0x1f1ff) return 2 * at;
    } else if (u == 0x1f3f4) {
      int next = at;
      while (next < length) {
        u = fl_utf8decode(text + next, text + length, NULL);
        const int bytes = fl_utf8len1(text[next]);
        if (bytes > length - next) break;
        next += bytes;
        if (u == 0xe007f) return next;
        if (u < 0xe0020 || u > 0xe007e) break;
      }
    }
  }
  while (at < length) {
    u = fl_utf8decode(text + at, text + length, NULL);
    if (u == 0x200d) {
      at += min(fl_utf8len1(text[at]), length - at);
      if (at < length) at += min(fl_utf8len1(text[at]), length - at);
    } else if ((u >= 0xfe00 && u <= 0xfe0f) ||
               (u >= 0x1f3fb && u <= 0x1f3ff) || u == 0x20e3) {
      at += min(fl_utf8len1(text[at]), length - at);
    } else break;
  }
  return at;
}

static double cpv_text_width(const char *text, int length) {
  const int limit = cpv_text_batch_limit();
  if (length <= limit) return fl_width(text, length);
  double width = 0;
  for (int at = 0; at < length;) {
    const int count = cpv_text_batch_length(text + at, length - at, limit);
    width += fl_width(text + at, count);
    at += count;
  }
  return width;
}

static void cpv_text_draw(const char *text, int length, int x, int y, int left, int right) {
  const int limit = cpv_text_batch_limit();
  if (length <= limit) {
    fl_draw(text, length, x, y);
    return;
  }
  const int overhang = 2 * fl_height();
  double pen = x;
  for (int at = 0; at < length;) {
    const int count = cpv_text_batch_length(text + at, length - at, limit);
    const double width = fl_width(text + at, count);
    if (pen + width + overhang >= left && pen - overhang <= right)
      fl_draw(text + at, count, (int)pen, y);
    pen += width;
    at += count;
    if (pen - overhang > right) break;
  }
}
]=])

replace_once([=[  IS_UTF8_ALIGNED(string)

  const Style_Table_Entry *styleRec = NULL;]=]
  [=[  IS_UTF8_ALIGNED(string)

  const int fill_left = max(X, text_area.x);
  const int fill_right = min(toX, text_area.x + text_area.w);
  const Style_Table_Entry *styleRec = NULL;]=])
replace_once("    clear_rect( style, X, Y, toX - X, mMaxsize );"
  "    if (fill_right > fill_left)\n      clear_rect( style, fill_left, Y, fill_right - fill_left, mMaxsize );")
replace_once("    fl_rectf( X, Y, toX - X, mMaxsize );"
  "    if (fill_right > fill_left)\n      fl_rectf( fill_left, Y, fill_right - fill_left, mMaxsize );")
replace_once("    fl_draw( string, nChars, X, baseline);"
  "    cpv_text_draw(string, nChars, X, baseline, text_area.x, text_area.x + text_area.w);")
replace_once("  return fl_width( string, length );"
  "  return cpv_text_width(string, length);")

replace_once([=[  // TODO: use binary search which may be quicker.
  int i = 0;
  int last_w = 0;       // STR #2788
  while (i<len) {
    const char *next = fl_utf8_next_composed_char(s + i, s + len);
    int cl = next - (s+i);
    int w = int( string_width(s, i+cl, style) );
    if (w>x) {
      if (cursor_pos && (w-x < x-last_w)) return i+cl; // STR #2788
      return i;
    }
    last_w = w;        // STR #2788
    i += cl;
  }
  return len;
]=] [=[  // Skip measured long-run batches before the original local search.
  // Repeated whole-prefix measurements would otherwise be quadratic at a
  // far-right click on a multi-megabyte line.
  int skipped = 0;
  double skipped_width = 0;
  string_width(s, 0, style); // select this style's font for the batch limit
  const int limit = cpv_text_batch_limit();
  const bool batched = len > limit;
  if (batched) {
    while (skipped < len) {
      const int count = cpv_text_batch_length(s + skipped, len - skipped, limit);
      const double width = fl_width(s + skipped, count);
      if ((int)(skipped_width + width) > x || skipped + count == len) {
        s += skipped;
        len = count;
        break;
      }
      skipped_width += width;
      skipped += count;
    }
  }
  int i = 0;
  int last_w = (int)skipped_width;
  while (i<len) {
    const int cl = batched ? cpv_text_cluster_length(s + i, len - i) :
                   int(fl_utf8_next_composed_char(s + i, s + len) - (s + i));
    int w = int(skipped_width + string_width(s, i+cl, style));
    if (w>x) {
      if (cursor_pos && (w-x < x-last_w)) return skipped+i+cl; // STR #2788
      return skipped+i;
    }
    last_w = w;        // STR #2788
    i += cl;
  }
  return skipped+len;
]=])

file(WRITE "${source_path}" "${source}")
message(STATUS "Applied bounded Source line drawing to pinned FLTK 1.4.5")
