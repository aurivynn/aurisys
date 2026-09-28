#pragma once

// interactive shell
namespace terminal {

void run(); // never returns
void printf(const char* fmt, ...);
void print(const char* string);

void env_set(const char* name_equals_value);
void env_assign(const char* name, const char* value);
void env_unset(const char* name);
int env_count();
const char** env_vector();

const char* getenv_from_shell(const char* name);

bool set_env_in_shell(const char* name, const char* value);

} // namespace terminal