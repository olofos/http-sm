#ifndef TEST_UTIL_H_
#define TEST_UTIL_H_

int open_tmp_file(void);
int write_tmp_file(const char *s);
int write_tmp_file_n(const char *s[]);
int write_socket(const char *s);
int write_socket_n(const char *s[]);
void close_socket(int fd);

const char *get_file_content(int fd);

#define assert_string_prefix_equal(pre, s) do { assert_true(strncmp(pre,s,strlen(pre)) == 0); } while(0)

#define assert_string_contains_substring(sub, s) do { assert_non_null(strstr(s, sub)); } while(0)


#endif
