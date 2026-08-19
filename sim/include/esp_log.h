#pragma once
#include <cstdio>

#define ESP_LOGE(tag, format, ...) fprintf(stderr, "E (%s) " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fprintf(stderr, "W (%s) " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) fprintf(stdout, "I (%s) " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, format, ...) do { (void)(tag); } while (0)
