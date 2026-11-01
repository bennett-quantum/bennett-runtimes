/* Deliberate global array OOB. Unlike an intra-struct overwrite, this also
 * crosses an ASan global redzone when the full ASan port becomes available.
 * Plain Linux maps the trailing bytes in the .bss page, so no native trap. */
int values[2];
int main(void) { volatile int index=2; values[index]=7; return 0; }
