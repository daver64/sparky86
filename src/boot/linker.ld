OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x00000000;

    .text : {
        boot.o(.text)    /* Force assembly bootstrap to load first at 0x0 */
        *(.text)
    }

    .rodata : { *(.rodata) }
    .data   : { *(.data) }

    /DISCARD/ : {
        *(*.eh_frame)
        *(*.note*)
        *(*.comment)
    }
}
