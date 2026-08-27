__attribute__((section(".text.start")))
void contract_entry(void) {
    __asm__ volatile ("cli\n\t hlt" ::: "memory");
    __builtin_unreachable();
}
