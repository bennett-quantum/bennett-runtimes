/* Deliberate global out-of-bounds store, within the mapped backing object. */
struct { int values[2]; int backing; } storage;
int main(void) { volatile int index=2; storage.values[index]=7; return 0; }
