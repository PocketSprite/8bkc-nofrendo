#include "statefile_wrapper.h"
#include "appfs.h"
#include "string.h"

#define TMPNAME "__nofrendo_state.tmp"

typedef struct {
	appfs_handle_t fd;
	size_t pos;
	int isWrite;
	char name[256];
} statefile_desc_t;


FILE *statefile_fopen(const char *pathname, const char *mode) {
	printf("Wrapper: open %s mode %s\n", pathname, mode);
	statefile_desc_t *s=calloc(sizeof(statefile_desc_t), 1);
	if (!s) goto err;
	strncpy(s->name, pathname, 256);
	if (mode[0]=='r') {
		if (!appfsExists(pathname)) goto err;
		s->fd=appfsOpen(pathname);
	} else if (mode[0]=='w') {
		appfsDeleteFile(TMPNAME);
		if (appfsCreateFile(TMPNAME, 1<<16, &s->fd)!=ESP_OK) goto err;
		s->isWrite=1;
		appfsErase(s->fd, 0, (1<<16));
	} else {
		goto err;
	}
	return (FILE*)s;
err:
	printf("Wrapper: open failed\n");
	free(s);
	return NULL;
}

int statefile_fclose(FILE *stream) {
	statefile_desc_t *s=(statefile_desc_t*)stream;
	if (s->isWrite) {
		appfsClose(s->fd);
		appfsRename(TMPNAME, s->name);
	}
	free(s);
	return 0;
}

size_t statefile_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	statefile_desc_t *s=(statefile_desc_t*)stream;
	printf("Wrapper: pos %d reading %d\n", s->pos, size*nmemb);
	if (size*nmemb==0) return nmemb;
	if (appfsRead(s->fd, s->pos, (uint8_t*)ptr, size*nmemb)==ESP_OK) {
		s->pos+=(size*nmemb);
		return nmemb;
	} else {
		return 0;
	}
}

size_t statefile_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
	statefile_desc_t *s=(statefile_desc_t*)stream;
	printf("Wrapper: pos %d writing %d\n", s->pos, size*nmemb);
	if (size*nmemb==0) return nmemb;
	if (appfsWrite(s->fd, s->pos, ptr, nmemb*size)==ESP_OK) {
		s->pos+=(size*nmemb);
		return nmemb;
	} else {
		return 0;
	}
}

int statefile_fseek(FILE *stream, long offset, int whence) {
	statefile_desc_t *s=(statefile_desc_t*)stream;
	int r=s->pos;
	if (whence==SEEK_SET) {
		s->pos=offset;
	} else if (whence==SEEK_CUR) {
		s->pos+=offset;
	} else if (whence==SEEK_END) {
		abort(); //not implemented
	}
	printf("Wrapper: seek from %d to %d\n", r, s->pos);
	return 0;
}
