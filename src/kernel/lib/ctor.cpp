extern "C" int __cxa_atexit(void (*)(void*), void*, void*) {
	return 0; // we never exit
}

extern "C" void __cxa_pure_virtual() {
	for (;;)
		asm volatile("hlt"); // shoudnt happen
}

void* __dso_handle = nullptr;