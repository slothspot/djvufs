#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <errno.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libdjvu/ddjvuapi.h>

#define PNG_DEBUG 3
#include <png.h>

const unsigned long CACHE_SIZE = 20 * 1024 * 1024;
#define logi(...) fprintf(((struct djvufs_data*)fuse_get_context()->private_data)->logf,__VA_ARGS__);fflush(((struct djvufs_data*)fuse_get_context()->private_data)->logf);

struct djvufs_data {
    FILE *logf;
    ddjvu_context_t *context;
    ddjvu_document_t *document;
};

void djvufs_page_to_png(int pagenum){
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_ptr){logi("No png_ptr\n"); return; }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr){logi("No info_ptr\n"); return; }
  if(setjmp(png_jmpbuf(png_ptr))){logi("No 1st jump\n"); return;}
  char buf[256];
  memset(buf, 0, 256);
  sprintf(buf, "/tmp/%d.png", pagenum+1);
  FILE *f = fopen(buf, "rb");
  if(f){ fclose(f); return; }
  f = fopen(buf, "wb");
  if(!f){logi("No f\n"); return;}
  png_init_io(png_ptr, f);
  if(setjmp(png_jmpbuf(png_ptr))){logi("No 2nd jump\n"); return;}
  djvufs_data *priv_data = reinterpret_cast<djvufs_data*>(fuse_get_context()->private_data);
  ddjvu_pageinfo_t pi;
  if(DDJVU_JOB_OK != ddjvu_document_get_pageinfo(priv_data->document, pagenum, &pi)){logi("pageinfo failed\n"); return;}
  logi("PI=> W: %d, H: %d\n", pi.width, pi.height);  
  ddjvu_page_t *page = ddjvu_page_create_by_pageno(priv_data->document, pagenum);
  int width = ddjvu_page_get_width(page);
  if(width == 0) width = pi.width;
  int height = ddjvu_page_get_height(page);
  if(height == 0) height = pi.height;
  if(width == 0 || height == 0){logi("Invalid page dimensions\n"); return;}
  int bit_depth = 8;
  int color_type = PNG_COLOR_TYPE_RGB;
  logi("W: %d, H: %d, D: %d, T: %d\n", width, height, bit_depth, color_type);
  png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_write_info(png_ptr, info_ptr);
  if(setjmp(png_jmpbuf(png_ptr))){logi("No 3rd jump\n"); return;}
  unsigned char *data = new unsigned char[width * height * 3];
  png_bytepp row_pointers = (png_bytepp) malloc(sizeof(png_bytep) * height);
  int y = 0;
  for (y=0; y<height; y++) row_pointers[y] = (png_bytep) malloc(width*3);

  ddjvu_rect_t rect;
  rect.x = rect.y = 0;
  rect.w = width;
  rect.h = height;
  ddjvu_format_t *format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, NULL);
  ddjvu_format_set_row_order(format, 1);
  ddjvu_format_set_y_direction(format, 1);
  while(!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rect, &rect, format, width * 3, reinterpret_cast<char*>(data))){ sleep(1); }
  for(y = 0; y < height; y++){
    memcpy(row_pointers[y], data + (y * width * 3) , width*3);
  }
  logi("Page was rendered\n");
  png_write_image(png_ptr, row_pointers);
  logi("Image data was written\n");
  ddjvu_format_release(format);
  logi("Format was released\n");
  for (y=0; y<height; y++) free(row_pointers[y]);
  free(row_pointers);
  delete data;
  logi("Renderer data was released\n");
  if(setjmp(png_jmpbuf(png_ptr))){logi("No 4th jump\n"); return;}
  png_write_end(png_ptr, NULL);
  fclose(f);
  logi("djvufs_to_png end\n");
}

int djvufs_getattr(const char *path, struct stat *stbuf){
    logi("getattr: %s\n", path);
    djvufs_data *data = reinterpret_cast<djvufs_data*>(fuse_get_context()->private_data);
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path, "/") == 0){
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
	int page = atoi(path+1);
	if(page > 0 && page <= ddjvu_document_get_pagenum(data->document)){
	  ddjvu_pageinfo_t pi;
	  if(DDJVU_JOB_OK == ddjvu_document_get_pageinfo(data->document, page-1, &pi)){
	    logi("Page: %d [ width => %d; height => %d; dpi => %d; rotation => %d; version => %d; ]\n", page, pi.width, pi.height, pi.dpi, pi.rotation, pi.version);
	    djvufs_page_to_png(page-1);
	    char fn[256];
	    memset(fn, 0, 256);
	    sprintf(fn, "/tmp/%d.png", page);
	    FILE *f = fopen(fn, "rb");
	    if(!f){
	      stbuf->st_size = 0;
	    } else {
	      fseek(f, 0, SEEK_END);
	      stbuf->st_size = ftell(f);
	      fclose(f);
	    }
	  } else stbuf->st_size = 0;
	} else
	  stbuf->st_size = 0;
    }
    return res;
}

int djvufs_open(const char *path, struct fuse_file_info *fi){
  logi("open: %s\n", path);
  djvufs_data *data = reinterpret_cast<djvufs_data *>(fuse_get_context()->private_data);
  char *endp = NULL;
  int page = strtol(path+1, &endp, 10);
  logi("guess page: %d [errno: %d]\n", page, errno);
  if(errno == EINVAL || (path+1) == endp || page <= 0 || page > ddjvu_document_get_pagenum(data->document)){
    logi("ENOENT for %s\n", path);
    return -ENOENT;
  }
  if((fi->flags & 3) != O_RDONLY){
    logi("EACCESS for %s [ %d ]\n", path, fi->flags);
    return -EACCES;
  }
  fi->direct_io = 0;
  fi->keep_cache = 1;
  fi->nonseekable = 0;
  logi("0 for %s\n", path);
  return 0;
}

int djvufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
  logi("read(path=> %s, [DATA_PTR], size => %d, offset => %d )\n", path, size, offset);
  struct djvufs_data *data = reinterpret_cast<djvufs_data*>(fuse_get_context()->private_data);
  char *endp = NULL;
  int pagenum = strtol(path+1, &endp, 10);
  logi("guess page: %d [errno: %d]\n", pagenum, errno);
  if(errno == EINVAL || (path+1) == endp || pagenum <= 0 || pagenum > ddjvu_document_get_pagenum(data->document)){
    logi("ENOENT for %s\n", path);
    return -ENOENT;
  }
  djvufs_page_to_png(pagenum-1);
  
  char fn[256];
  memset(fn, 0, 256);
  sprintf(fn, "/tmp/%d.png", pagenum);
  FILE *f = fopen(fn, "rb");
  if(!f){logi("No f\n"); return 0;}
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  rewind(f);
  if(offset < fsize){
    if(offset + size > fsize) size = fsize - offset;
    logi("read size: %d\n", size);
    fseek(f, offset, SEEK_SET);
    size = fread(buf, 1, size, f);
  } else 
    size = 0;
  fclose(f);
  logi("read return: %d\n", size);
  return size;
}

int djvufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
    logi("readdir: %s\n", path);
    if(strcmp(path, "/") != 0)
        return -ENOENT;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    djvufs_data *priv_data = reinterpret_cast<djvufs_data*>(fuse_get_context()->private_data);
    int pages = ddjvu_document_get_pagenum(priv_data->document);
    char name_buf[255];
    int i = 0;
    for(; i < pages; i++){
      sprintf(name_buf, "%d.png\0", i+1);
      filler(buf, name_buf, NULL, 0);
    }
    return 0;
}

void* djvufs_init(struct fuse_conn_info *conn){
  char *path = reinterpret_cast<char*>(fuse_get_context()->private_data);
  djvufs_data *priv_data = new djvufs_data;
  priv_data->logf = fopen("/tmp/djvufs.log", "a");
  fprintf(priv_data->logf, "Init for path: %s\n", path);
  fflush(priv_data->logf);
  priv_data->context = ddjvu_context_create("djvufs");
  ddjvu_cache_set_size(priv_data->context, CACHE_SIZE);
  priv_data->document = ddjvu_document_create_by_filename_utf8(priv_data->context, path, TRUE);
  while(!ddjvu_document_decoding_done(priv_data->document)) sleep(1);
  free(path);
  return priv_data;
}

void djvufs_destroy(void* data){
  logi("Destroy\n");
  djvufs_data *pd = reinterpret_cast<djvufs_data*>(data);
  fclose(pd->logf);
  ddjvu_document_release(pd->document);
  ddjvu_context_release(pd->context);
  delete pd;
}

struct fuse_operations djvufs_ops = {
    /*.getattr = */djvufs_getattr,
    /*.readlink = */NULL,
    /*.getdir = */NULL,
    /*.mknod = */NULL,
    /*.mkdir = */NULL,
    /*.unlink = */NULL,
    /*.rmdir = */NULL,
    /*.symlink = */NULL,
    /*.rename = */NULL,
    /*.link = */NULL,
    /*.chmod = */NULL,
    /*.chown = */NULL,
    /*.truncate = */NULL,
    /*.utime = */NULL,
    /*.open = */djvufs_open,
    /*.read = */djvufs_read,
    /*.write = */NULL,
    /*.statfs = */NULL,
    /*.flush = */NULL,
    /*.release = */NULL,
    /*.fsync = */NULL,
    /*.setxattr = */NULL,
    /*.getxattr = */NULL,
    /*.listxattr = */NULL,
    /*.removexattr = */NULL,
    /*.opendir = */NULL,
    /*.readdir = */djvufs_readdir,
    /*.releasedir = */NULL,
    /*.fsyncdir = */NULL,
    /*.init = */djvufs_init, // init code should be placed here
    /*.destroy = */djvufs_destroy, // clean up code should be placed here
    /*.access = */NULL,
    /*.create = */NULL,
    /*.ftruncate = */NULL,
    /*.fgetattr = */NULL,
    /*.lock = */NULL,
    /*.utimens = */NULL,
    /*.bmap = */NULL,
    /*.flag_nullpath_ok = */0,
    /*.flag_reserved = */0,
    /*.ioctl = */NULL,
    /*.poll = */NULL,
};

int main(int argc, char** argv){
    printf("Target dir: %s\n", argv[argc-2]);
    printf("Source file: %s\n", argv[argc-1]);
    
  char *path = realpath(argv[argc-1], NULL);
  if(path == NULL){
      printf("Couldn't resolve source file path\n");
      return 1;
  }
    fuse_main(argc-1, argv, &djvufs_ops, path);
    return 0;
}
