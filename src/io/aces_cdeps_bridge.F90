module aces_cdeps_bridge_mod
    use iso_c_binding
    implicit none

    private

    public :: aces_cdeps_init
    public :: aces_cdeps_advance
    public :: aces_cdeps_read
    public :: aces_cdeps_finalize

    ! Use real CDEPS modules. These must be provided by the build system.
    ! Since we are in the JCSDA environment, we expect them to be available
    ! either from system-installed CDEPS or built as a dependency.
    ! If they are not found during compilation, it means the build environment
    ! is not set up correctly as per the "no mocks" requirement.

    ! Note: We use an interface to the existing CDEPS inline API symbols.
    interface
        subroutine cdeps_inline_init(config_file) bind(c, name="cdeps_inline_init")
            import :: c_char
            character(kind=c_char), intent(in) :: config_file(*)
        end subroutine
        subroutine cdeps_inline_read(buffer, stream_name) bind(c, name="cdeps_inline_read")
            import :: c_ptr, c_char
            type(c_ptr), value :: buffer
            character(kind=c_char), intent(in) :: stream_name(*)
        end subroutine
        subroutine cdeps_inline_advance(ymd, tod) bind(c, name="cdeps_inline_advance")
            import :: c_int
            integer(c_int), value :: ymd, tod
        end subroutine
        subroutine cdeps_inline_finalize() bind(c, name="cdeps_inline_finalize")
        end subroutine
    end interface

contains

    subroutine aces_cdeps_init(config_file) bind(c, name="aces_cdeps_init")
        character(kind=c_char), intent(in) :: config_file(*)
        call cdeps_inline_init(config_file)
    end subroutine

    subroutine aces_cdeps_advance(ymd, tod) bind(c, name="aces_cdeps_advance")
        integer(c_int), value :: ymd, tod
        call cdeps_inline_advance(ymd, tod)
    end subroutine

    subroutine aces_cdeps_read(buffer, stream_name) bind(c, name="aces_cdeps_read")
        type(c_ptr), value :: buffer
        character(kind=c_char), intent(in) :: stream_name(*)
        call cdeps_inline_read(buffer, stream_name)
    end subroutine

    subroutine aces_cdeps_finalize() bind(c, name="aces_cdeps_finalize")
        call cdeps_inline_finalize()
    end subroutine

end module
