!!
!! libgtool3.f90 -- interfaces defined in if_fortran.c
!!
!! Example:
!!    program main
!!      implicit none
!!      include 'libgtool3.f90'  !! <****
!!
!!      call gt3f_stop_on_error(1)
!!      (snip)
!!    end program main
!!
interface
   subroutine gt3f_stop_on_error(onoff)
     integer, intent(in) :: onoff
   end subroutine gt3f_stop_on_error

   subroutine gt3f_print_error
   end subroutine gt3f_print_error

   subroutine gt3f_open_output(iu, path, append)
     integer, intent(out) :: iu
     character(len=*), intent(in) :: path
     integer, intent(in) :: append
   end subroutine gt3f_open_output

   subroutine gt3f_close_output(iu, status)
     integer, intent(in) :: iu
     integer, intent(out) :: status
   end subroutine gt3f_close_output

   subroutine gt3f_close_output_all(status)
     integer, intent(out) :: status
   end subroutine gt3f_close_output_all

   subroutine gt3f_init_header(head)
     character, intent(out) :: head(1024)
   end subroutine gt3f_init_header

   subroutine gt3f_set_item(head, key, value)
     character, intent(out) :: head(1024)
     character(len=*), intent(in) :: key, value
   end subroutine gt3f_set_item

   subroutine gt3f_set_item_int(head, key, value)
     character, intent(out) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(in) :: value
   end subroutine gt3f_set_item_int

   subroutine gt3f_set_item_double(head, key, value)
     character, intent(out) :: head(1024)
     character(len=*), intent(in) :: key
     real(kind(0.d0)), intent(in) :: value
   end subroutine gt3f_set_item_double

   subroutine gt3f_set_item_date(head, key, year, mon, day, &
     &                           hour, mn, sec)
     character, intent(out) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(in) :: year, mon, day, hour, mn, sec
   end subroutine gt3f_set_item_date

   subroutine gt3f_set_item_miss(head, vmiss)
     character, intent(out) :: head(1024)
     real(kind(0.d0)), intent(in) :: vmiss
   end subroutine gt3f_set_item_miss

   subroutine gt3f_get_item(value, head, key, status)
     character(len=*), intent(out) :: value
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(out) :: status
   end subroutine gt3f_get_item

   subroutine gt3f_get_item_int(value, head, key, status)
     integer, intent(out) :: value
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(out) :: status
   end subroutine gt3f_get_item_int

   subroutine gt3f_get_item_double(value, head, key, status)
     real(kind(0.d0)), intent(out) :: value
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(out) :: status
   end subroutine gt3f_get_item_double

   subroutine gt3f_get_item_date(value, head, key, status)
     integer, intent(out) :: value(6)
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: key
     integer, intent(out) :: status
   end subroutine gt3f_get_item_date

   subroutine gt3f_write(iu, ptr, nx, ny, nz, head, dfmt, status)
     integer, intent(in) :: iu
     real(kind(0.d0)), intent(in) :: ptr(*)
     integer, intent(in) :: nx, ny, nz
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: dfmt
     integer, intent(out) :: status
   end subroutine gt3f_write

   subroutine gt3f_write_float(iu, ptr, nx, ny, nz, head, dfmt, status)
     integer, intent(in) :: iu
     real(kind(0.0)), intent(in) :: ptr(*)
     integer, intent(in) :: nx, ny, nz
     character, intent(in) :: head(1024)
     character(len=*), intent(in) :: dfmt
     integer, intent(out) :: status
   end subroutine gt3f_write_float

   subroutine gt3f_open_input(iu, path)
     integer, intent(out) :: iu
     character(len=*), intent(in) :: path
   end subroutine gt3f_open_input

   subroutine gt3f_open_input2(iu, path)
     integer, intent(out) :: iu
     character(len=*), intent(in) :: path
   end subroutine gt3f_open_input2

   subroutine gt3f_close_input(iu)
     integer, intent(in) :: iu
   end subroutine gt3f_close_input

   subroutine gt3f_close_input_all
   end subroutine gt3f_close_input_all

   subroutine gt3f_seek(iu, dest, whence, status)
     integer, intent(in) :: iu, dest, whence
     integer, intent(out) :: status
   end subroutine gt3f_seek

   subroutine gt3f_rewind(iu, status)
     integer, intent(in) :: iu
     integer, intent(out) :: status
   end subroutine gt3f_rewind

   subroutine gt3f_next(iu, status)
     integer, intent(in) :: iu
     integer, intent(out) :: status
   end subroutine gt3f_next

   subroutine gt3f_eof(iu, status)
     integer, intent(in) :: iu
     integer, intent(out) :: status
   end subroutine gt3f_eof

   subroutine gt3f_tell_input(iu, pos)
     integer, intent(in) :: iu
     integer, intent(out) :: pos
   end subroutine gt3f_tell_input

   subroutine gt3f_get_filename(iu, path, status)
     integer, intent(in) :: iu
     character(len=*), intent(out) :: path
     integer, intent(out) :: status
   end subroutine gt3f_get_filename

   subroutine gt3f_get_num_chunks(iu, num)
     integer, intent(in) :: iu
     integer, intent(out) :: num
   end subroutine gt3f_get_num_chunks

   subroutine gt3f_get_shape(iu, shape, status)
     integer, intent(in) :: iu
     integer, intent(out) :: shape(3), status
   end subroutine gt3f_get_shape

   subroutine gt3f_read_header(iu, head, status)
     integer, intent(in) :: iu
     character, intent(out) :: head(1024)
     integer, intent(out) :: status
   end subroutine gt3f_read_header

   subroutine gt3f_count_chunk(num, path)
     integer, intent(out) :: num
     character(len=*), intent(in) :: path
   end subroutine gt3f_count_chunk

   subroutine gt3f_read(iu, buf, miss, read_shape, &
     &                  xsize, ysize, zsize, &
     &                  xread, yread, zread, &
     &                  xoff, yoff, zoff, status)
     integer, intent(in) :: iu
     real(kind(0.d0)), intent(out) :: buf(*), miss
     integer, intent(out) :: read_shape(3)
     integer, intent(in) :: xsize, ysize, zsize, xread, yread, zread
     integer, intent(in) :: xoff, yoff, zoff
     integer, intent(out) :: status
   end subroutine gt3f_read

   subroutine gt3f_read_var(iu, buf, miss, read_shape, &
     &                      xsize, ysize, zsize, status)
     integer, intent(in) :: iu
     real(kind(0.d0)), intent(out) :: buf(*), miss
     integer, intent(out) :: read_shape(3)
     integer, intent(in) :: xsize, ysize, zsize
     integer, intent(out) :: status
   end subroutine gt3f_read_var

   subroutine gt3f_get_dimlen(length, name)
     integer, intent(out) :: length
     character(len=*), intent(in) :: name
   end subroutine gt3f_get_dimlen

   subroutine gt3f_get_grid(loc, locsize, name, status)
     real(kind(0.d0)), intent(out) :: loc(*)
     integer, intent(in) :: locsize
     character(len=*), intent(in) :: name
     integer, intent(out) :: status
   end subroutine gt3f_get_grid

   subroutine gt3f_get_weight(wght, wghtsize, name, status)
     real(kind(0.d0)), intent(out) :: wght(*)
     integer, intent(in) :: wghtsize
     character(len=*), intent(in) :: name
     integer, intent(out) :: status
   end subroutine gt3f_get_weight

   subroutine gt3f_get_gridbound(bnds, vsize, name, status)
     real(kind(0.d0)), intent(out) :: bnds(*)
     integer, intent(in) :: vsize
     character(len=*), intent(in) :: name
     integer, intent(out) :: status
   end subroutine gt3f_get_gridbound

   subroutine gt3f_get_calendar(calendar, name)
     integer, intent(out) :: calendar
     character(len=*), intent(in) :: name
   end subroutine gt3f_get_calendar

   subroutine gt3f_calendar_name(name, calendar)
     character(len=*), intent(out) :: name
     integer, intent(in) :: calendar
   end subroutine gt3f_calendar_name

   subroutine gt3f_guess_calendar(calendar, time, date)
     integer, intent(out) :: calendar
     real(kind(0.d0)), intent(in) :: time
     integer, intent(in) :: date(6)
   end subroutine gt3f_guess_calendar

   subroutine gt3f_check_date(status, year, mon, day, calendar)
     integer, intent(out) :: status
     integer, intent(in) :: year, mon, day, calendar
   end subroutine gt3f_check_date

   subroutine gt3f_set_basetime(year, mon, day, hh, mm, ss)
     integer, intent(in) :: year, mon, day, hh, mm, ss
   end subroutine gt3f_set_basetime

   subroutine gt3f_get_time(time, date, calendar, status)
     real(kind(0.d0)), intent(out) :: time
     integer, intent(in) :: date(6), calendar
     integer, intent(out) :: status
   end subroutine gt3f_get_time

   subroutine gt3f_get_date(date, time, calendar, status)
     integer, intent(out) :: date(6)
     real(kind(0.d0)), intent(in) :: time
     integer, intent(in) :: calendar
     integer, intent(out) :: status
   end subroutine gt3f_get_date

   subroutine gt3f_get_middate(date, date1, date2, calendar, status)
     integer, intent(out) :: date(6)
     integer, intent(in) :: date1(6), date2(6), calendar
     integer, intent(out) :: status
   end subroutine gt3f_get_middate

   subroutine gt3f_add_time(date, time, calendar, status)
     integer, intent(inout) :: date(6)
     real(kind(0.d0)), intent(in) :: time
     integer, intent(in) :: calendar
     integer, intent(out) :: status
   end subroutine gt3f_add_time
end interface
