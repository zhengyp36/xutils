#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <xutils/file_map.h>

#ifndef MIN
#define MIN(x,y) ((x) <= (y) ? (x) : (y))
#endif

static uint32_t
dump_dif64(void *_d1, void *_d2, void *_base1)
{
	uint8_t *d1 = _d1, *d2 = _d2, *base1 = _base1;

	uint32_t cnt = 0;
	for (int i = 0; i < 8; i++) {
		if (d1[i] != d2[i]) {
			printf("%08lx : %02x -- %02x\n",
			    &d1[i] - base1, d1[i], d2[i]);
			cnt++;
		}
	}

	return (cnt);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("Usage: %s <file1> <file2>\n", basename(argv[0]));
		return (-1);
	}

	file_map_t fm1, fm2;
	file_map_init(&fm1, argv[1]);
	file_map_init(&fm2, argv[2]);

	int ret = -1;
	if (file_map_open(&fm1, FM_RDONLY) || file_map_open(&fm2, FM_RDONLY))
		goto done;

	uint32_t diff_cnt = 0;
	size_t size = MIN(fm1.size, fm2.size);

	uint64_t *d1 = fm1.buf, *d2 = fm2.buf;
	uint64_t cnt_u64 = size / sizeof(uint64_t);
	for (uint64_t i = 0; i < cnt_u64; i++)
		if (d1[i] != d2[i])
			diff_cnt += dump_dif64(&d1[i], &d2[i], d1);

	uint8_t cnt_u8 = size % sizeof(uint64_t);
	if (cnt_u8) {
		uint8_t c1[8] = {0}, c2[8] = {0};
		memcpy(c1, &d1[cnt_u64], cnt_u8);
		memcpy(c2, &d2[cnt_u64], cnt_u8);
		diff_cnt += dump_dif64(c1, c2, d1);
	}

	if (!diff_cnt) {
		ret = 0;
		printf("%s and %s are same\n", argv[1], argv[2]);
	} else
		printf("%s and %s have %u bytes different\n",
		    argv[1], argv[2], diff_cnt);

done:
	file_map_fini(&fm1);
	file_map_fini(&fm2);
	return (ret);
}
