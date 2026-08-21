/*
 * Reference linker-section fragment only.
 *
 * A target project must define RETAINED_RAM from its verified MCU memory map
 * and must verify startup code does not clear this NOLOAD region for the reset
 * classes the project claims to support.
 */
SECTIONS
{
    .incident_ram (NOLOAD) : ALIGN(8)
    {
        __incident_ram_start__ = .;
        KEEP(*(.incident_ram))
        __incident_ram_end__ = .;
    } > RETAINED_RAM
}
