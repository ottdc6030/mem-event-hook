//Every time you add a new (non-main) override in define_override.c, make an ID for it here
enum OVERRIDE_ID {
    MALLOC,
    CALLOC,
    FREE,
    THREAD_CREATE,
    THREAD_EXIT,
    EXIT,
    MAX_OVERRIDE_VAL //Not an actual override, just easy way to get size of enum.
};

void push_event(int event_type, void* data);








