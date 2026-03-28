/*
                  Linker File for Renesas FSP
*/

INCLUDE memory_regions.ld
INCLUDE fsp_gen.ld

SECTIONS
{
    .sram1_buffer 0x20052000 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram1_buffer_start = .;
        *(.sram1_buffer*)
        . = ALIGN(32);
        __sram1_buffer_end = .;
    } > RAM
}
