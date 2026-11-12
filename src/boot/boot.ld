SECTIONS
{
    . = 0x7C00;
    .boot : { *(.text*) *(.rodata*) *(.data*) }
    /DISCARD/ : { *(*) }
}
