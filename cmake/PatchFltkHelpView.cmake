# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
# Patch only the extracted, pinned FLTK 1.4.5 build source. The vendor archive
# and its upstream checksums remain unchanged.
if(NOT DEFINED CPV_FLTK_SOURCE_DIR)
  message(FATAL_ERROR "CPV_FLTK_SOURCE_DIR is required")
endif()
set(source_path "${CPV_FLTK_SOURCE_DIR}/src/Fl_Help_View.cxx")
set(header_path "${CPV_FLTK_SOURCE_DIR}/FL/Fl_Help_View.H")
file(READ "${source_path}" source)
file(READ "${header_path}" header)
string(FIND "${source}" "CPV_HELP_VIEW_LAYOUT_210" applied)
if(NOT applied EQUAL -1)
  string(FIND "${header}" "const char *table, int available_width" header_applied)
  if(header_applied EQUAL -1)
    message(FATAL_ERROR "Incomplete FLTK preview layout patch")
  endif()
  return()
endif()

function(replace_once variable old new)
  string(FIND "${${variable}}" "${old}" first)
  if(first EQUAL -1)
    message(FATAL_ERROR "Pinned FLTK layout patch context is missing")
  endif()
  string(LENGTH "${old}" old_length)
  math(EXPR after "${first} + ${old_length}")
  string(SUBSTRING "${${variable}}" ${after} -1 tail)
  string(FIND "${tail}" "${old}" duplicate)
  if(NOT duplicate EQUAL -1)
    message(FATAL_ERROR "Pinned FLTK layout patch context is ambiguous")
  endif()
  string(REPLACE "${old}" "${new}" changed "${${variable}}")
  set(${variable} "${changed}" PARENT_SCOPE)
endfunction()

function(replace_section variable first last replacement)
  string(FIND "${${variable}}" "${first}" start)
  string(FIND "${${variable}}" "${last}" stop)
  if(start EQUAL -1 OR stop EQUAL -1 OR NOT stop GREATER start)
    message(FATAL_ERROR "Pinned FLTK layout patch section is missing")
  endif()
  math(EXPR length "${stop} - ${start}")
  string(SUBSTRING "${${variable}}" ${start} ${length} old)
  replace_once(${variable} "${old}" "${replacement}")
  set(${variable} "${${variable}}" PARENT_SCOPE)
endfunction()

replace_once(header
  "const char *table);"
  "const char *table, int available_width);")
replace_once(source
  "const char *table)           // I - Pointer to start of table"
  "const char *table,           // I - Pointer to start of table\n                           int available_width)         // I - Viewport space for the table")

replace_once(source [=[  // Reset document width...
  int scrollsize = scrollbar_size_ ? scrollbar_size_ : Fl::scrollbar_size();
  hsize_ = w() - scrollsize - Fl::box_dw(b);

  done = 0;
]=] [=[  // CPV_HELP_VIEW_LAYOUT_210: table percentages use the viewport, not the
  // growing document extent. A second pass reserves a vertical scrollbar
  // only when the completed first layout needs one.
  const int scrollsize = scrollbar_size_ ? scrollbar_size_ : Fl::scrollbar_size();
  bool vertical_needed = false;
  int viewport_width;
  for (;;) {
  viewport_width = w() - Fl::box_dw(b) - (vertical_needed ? scrollsize : 0);
  if (viewport_width < 1) viewport_width = 1;
  hsize_ = viewport_width;

  done = 0;
]=])
replace_once(source
  "format_table(&table_width, columns, start);"
  "format_table(&table_width, columns, start, viewport_width - xx - 4);")
replace_once(source
  "if ((xx + table_width) > hsize_) {"
  "if ((xx + table_width + 4) > hsize_) {")
replace_once(source
  "hsize_ = xx + table_width;"
  "hsize_ = xx + table_width + 4;")
replace_once(source
  "table_offset = (hsize_ - table_width) / 2 - textsize_;"
  "table_offset = (viewport_width - xx - table_width - 4) / 2;\n                  if (table_offset < 0) table_offset = 0;")
replace_once(source
  "table_offset = hsize_ - table_width - textsize_;"
  "table_offset = viewport_width - xx - table_width - 4;\n                  if (table_offset < 0) table_offset = 0;")
replace_once(source [=[  // Make sure that the last block will have the correct height.
  if (hh > block->h) block->h = hh;
]=] [=[  // Make sure that the last block will have the correct height.
  if (hh > block->h) block->h = hh;
  const int content_height = h() - Fl::box_dh(b) -
                             (hsize_ > viewport_width ? scrollsize : 0);
  if (!vertical_needed && size_ >= content_height) {
    vertical_needed = true;
    continue;
  }
  break;
  }
]=])
replace_section(source
  "  int dw = Fl::box_dw(b) + ss;\n"
  "  // Reset scrolling if it needs to be...\n"
  [=[  const bool horizontal_needed = hsize_ > viewport_width;
  if (horizontal_needed) {
    hscrollbar_.show();
    hscrollbar_.resize(x() + Fl::box_dx(b), y() + h() - ss - dy,
                      viewport_width, ss);
  } else hscrollbar_.hide();
  if (vertical_needed) {
    scrollbar_.show();
    int bar_height = h() - Fl::box_dh(b) - (horizontal_needed ? ss : 0);
    if (bar_height < 1) bar_height = 1;
    scrollbar_.resize(x() + w() - ss - dx, y() + Fl::box_dy(b),
                      ss, bar_height);
  } else scrollbar_.hide();

]=])
replace_once(source
  "int tempw = w() - ss - Fl::box_dw(b);"
  "int tempw = viewport_width;")

# PRE content must retain a background as wide as its complete unwrapped line.
# Ordinary cells keep the existing longest-word minimum.
replace_once(source [=[      if (temp_width > minwidths[column])
        minwidths[column] = temp_width;

      width += temp_width;
]=] [=[      width += temp_width;
      const int minimum = pre ? width : temp_width;
      if (minimum > minwidths[column])
        minwidths[column] = minimum;
]=])
replace_once(source [=[      else if (buf.cmp("/CENTER") ||
               buf.cmp("/P") ||
               buf.cmp("/H1") ||
               buf.cmp("/H2") ||
               buf.cmp("/H3") ||
               buf.cmp("/H4") ||
               buf.cmp("/H5") ||
               buf.cmp("/H6") ||
               buf.cmp("/PRE") ||
               buf.cmp("/UL") ||
               buf.cmp("/OL") ||
               buf.cmp("/DL"))
      {
        width     = 0;
]=] [=[      else if (buf.cmp("/CENTER") ||
               buf.cmp("/P") ||
               buf.cmp("/H1") ||
               buf.cmp("/H2") ||
               buf.cmp("/H3") ||
               buf.cmp("/H4") ||
               buf.cmp("/H5") ||
               buf.cmp("/H6") ||
               buf.cmp("/PRE") ||
               buf.cmp("/UL") ||
               buf.cmp("/OL") ||
               buf.cmp("/DL"))
      {
        if (buf.cmp("/PRE")) pre = 0;
        width     = 0;
]=])
replace_once(source [=[    else if (isspace((*ptr)&255))
    {
      needspace = 1;

      ptr ++;
    }
]=] [=[    else if (isspace((*ptr)&255))
    {
      if (pre && incell) {
        width += (int)fl_width(' ');
        if (width > minwidths[column]) minwidths[column] = width;
        if (width > max_width) max_width = width;
      } else needspace = 1;

      ptr ++;
    }
]=])

replace_section(source
  "  // Now that we have scanned the entire table, adjust the table and\n"
  "#ifdef DEBUG\n  printf(\"FINAL table_width = %d\\n\", *table_width);"
  [=[  if (num_columns == 0) return;

  // Cell positioning adds textsize_ + 3 before the first column, six pixels
  // between columns, and three pixels after the final content edge. Include
  // that painted extent in both the width budget and overflow measurement.
  const int overhead = textsize_ + 6 * num_columns;
  int minimum_width = 0;
  width = 0;
  for (column = 0; column < num_columns; column ++) {
    if (columns[column] < minwidths[column]) columns[column] = minwidths[column];
    width += columns[column];
    minimum_width += minwidths[column];
  }

  int target_width = width + overhead;
  if (get_attr(table + 6, "WIDTH", attr, sizeof(attr))) {
    int requested = atoi(attr);
    if (attr[0] && attr[strlen(attr) - 1] == '%') {
      if (requested < 0) requested = 0;
      if (requested > 100) requested = 100;
      requested = available_width * requested / 100;
    }
    if (requested > 0) target_width = requested;
  } else if (target_width > available_width) target_width = available_width;
  target_width -= overhead;
  if (target_width < minimum_width) target_width = minimum_width;

  if (width < target_width) {
    const int extra = target_width - width;
    for (column = 0; column < num_columns; column ++)
      columns[column] += extra / num_columns;
    columns[num_columns - 1] += extra % num_columns;
  } else if (width > target_width) {
    const int flexible_width = width - minimum_width;
    const int extra = target_width - minimum_width;
    int assigned = 0;
    for (column = 0; column < num_columns; column ++) {
      const int share = flexible_width ? (int)((long long)extra *
                        (columns[column] - minwidths[column]) / flexible_width) : 0;
      columns[column] = minwidths[column] + share;
      assigned += share;
    }
    // Preserve the requested width exactly when proportional division rounds.
    columns[num_columns - 1] += extra - assigned;
  }
  *table_width = overhead;
  for (column = 0; column < num_columns; column ++)
    *table_width += columns[column];

]=])

replace_once(source [=[  int scrollsize = scrollbar_size_ ? scrollbar_size_ : Fl::scrollbar_size();
  if (hsize_ < (w() - scrollsize) || left < 0)
    left = 0;
  else if (left > hsize_)
    left = hsize_;

  leftline_ = left;

  hscrollbar_.value(leftline_, w() - scrollsize, 0, hsize_);
]=] [=[  const int scrollsize = scrollbar_.visible() ?
                        (scrollbar_size_ ? scrollbar_size_ : Fl::scrollbar_size()) : 0;
  const Fl_Boxtype b = box() ? box() : FL_DOWN_BOX;
  int viewport_width = w() - Fl::box_dw(b) - scrollsize;
  if (viewport_width < 1) viewport_width = 1;
  const int maximum = hsize_ > viewport_width ? hsize_ - viewport_width : 0;
  if (left < 0) left = 0;
  else if (left > maximum) left = maximum;

  leftline_ = left;
  hscrollbar_.value(leftline_, viewport_width, 0, hsize_);
]=])

file(WRITE "${source_path}" "${source}")
file(WRITE "${header_path}" "${header}")
message(STATUS "Applied Copyparty Viewer preview layout correction to pinned FLTK 1.4.5")
