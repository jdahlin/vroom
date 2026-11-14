extern int external_data;
extern int external_function(void);
int *const const_data_pointer = &external_data;
int (*const const_function_pointer)(void) = external_function;
const char *const const_string_table[] = { "one", "two" };
const int plain_constant = 7;
int *mutable_pointer = &external_data;
