OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x100000;

    .pehdr : {
        *(.pehdr)
    }

    . = 0x101000;
    .text : {
        *(.text)
    }

    . = 0x104000;
    .payload : {
        *(.payload)
    }
}
