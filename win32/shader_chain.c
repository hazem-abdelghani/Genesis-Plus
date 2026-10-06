/****************************************************************************
 *  Genesis Plus -- Win32 GUI frontend
 *
 *  shader_chain.c -- see shader_chain.h.
 ****************************************************************************/

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#define LIBRA_RUNTIME_D3D11
#include <d3d11.h>
#include "librashader/librashader.h"
#include "shader_chain.h"

static HMODULE                          dll;
static PFN_libra_preset_create          pfn_preset_create;
static PFN_libra_preset_free            pfn_preset_free;
static PFN_libra_d3d11_filter_chain_create pfn_chain_create;
static PFN_libra_d3d11_filter_chain_frame  pfn_chain_frame;
static PFN_libra_d3d11_filter_chain_free   pfn_chain_free;
static PFN_libra_error_write            pfn_error_write;
static PFN_libra_error_free_string      pfn_error_free_string;
static PFN_libra_error_free             pfn_error_free;

static libra_d3d11_filter_chain_t chain;
static char                       preset_path[512];
static char                       last_error[256];
static size_t                     frame_count;

static void set_error_from_libra(libra_error_t err)
{
  char *msg = NULL;

  last_error[0] = '\0';
  if (!err) return;

  if (pfn_error_write && pfn_error_write(err, &msg) == 0 && msg)
  {
    lstrcpynA(last_error, msg, sizeof(last_error));
    if (pfn_error_free_string) pfn_error_free_string(&msg);
  }
  else
  {
    lstrcpynA(last_error, "librashader reported an error", sizeof(last_error));
  }

  if (pfn_error_free) pfn_error_free(&err);
}

void shader_chain_init(void)
{
  if (dll) return;   /* already attempted */

#ifndef _WIN64
  return;            /* 32-bit build: shaders are not offered at all */
#endif

  dll = LoadLibraryA("librashader.dll");
  if (!dll) return;

  /* GetProcAddress returns a function pointer type incompatible (by strict
     ANSI aliasing rules) with the PFN_* types by a direct cast in one step
     on some compilers' warning settings -- an intermediate void* silences
     that without changing what actually happens (a plain pointer-sized
     bit copy either way). */
  {
    void *p;

    p = (void *)GetProcAddress(dll, "libra_preset_create");
    pfn_preset_create = (PFN_libra_preset_create)p;
    p = (void *)GetProcAddress(dll, "libra_preset_free");
    pfn_preset_free = (PFN_libra_preset_free)p;
    p = (void *)GetProcAddress(dll, "libra_d3d11_filter_chain_create");
    pfn_chain_create = (PFN_libra_d3d11_filter_chain_create)p;
    p = (void *)GetProcAddress(dll, "libra_d3d11_filter_chain_frame");
    pfn_chain_frame = (PFN_libra_d3d11_filter_chain_frame)p;
    p = (void *)GetProcAddress(dll, "libra_d3d11_filter_chain_free");
    pfn_chain_free = (PFN_libra_d3d11_filter_chain_free)p;
    p = (void *)GetProcAddress(dll, "libra_error_write");
    pfn_error_write = (PFN_libra_error_write)p;
    p = (void *)GetProcAddress(dll, "libra_error_free_string");
    pfn_error_free_string = (PFN_libra_error_free_string)p;
    p = (void *)GetProcAddress(dll, "libra_error_free");
    pfn_error_free = (PFN_libra_error_free)p;
  }

  if (!pfn_preset_create || !pfn_preset_free || !pfn_chain_create ||
      !pfn_chain_frame || !pfn_chain_free)
  {
    /* A librashader.dll too old/new to export what this was built against --
       treat exactly like it wasn't found at all. */
    FreeLibrary(dll);
    dll = NULL;
  }
}

int shader_chain_available(void)
{
  return dll != NULL;
}

void shader_chain_unload(void)
{
  if (chain)
  {
    pfn_chain_free(&chain);
    chain = NULL;
  }
}

int shader_chain_load(void *device, const char *preset_file)
{
  libra_shader_preset_t preset = NULL;
  libra_error_t err;

  shader_chain_unload();
  last_error[0] = '\0';
  frame_count = 0;

  if (!dll)
  {
    lstrcpynA(last_error, "librashader.dll was not found", sizeof(last_error));
    return 0;
  }

  err = pfn_preset_create(preset_file, &preset);
  if (err) { set_error_from_libra(err); return 0; }

  err = pfn_chain_create(&preset, (ID3D11Device *)device, NULL, &chain);
  if (err)
  {
    set_error_from_libra(err);
    chain = NULL;
    return 0;
  }

  lstrcpynA(preset_path, preset_file, sizeof(preset_path));
  return 1;
}

int shader_chain_active(void)
{
  return chain != NULL;
}

const char *shader_chain_preset_path(void)
{
  return preset_path;
}

int shader_chain_render(void *context, void *input, void *output,
                        int x, int y, int w, int h)
{
  libra_viewport_t viewport;
  libra_error_t err;

  if (!chain) return 0;
  if (w < 1 || h < 1) return 0;

  viewport.x = (float)x;
  viewport.y = (float)y;
  viewport.width = (UINT)w;
  viewport.height = (UINT)h;

  err = pfn_chain_frame(&chain, (ID3D11DeviceContext *)context, frame_count,
                        (ID3D11ShaderResourceView *)input,
                        (ID3D11RenderTargetView *)output,
                        &viewport, NULL, NULL);
  frame_count++;

  if (err) { set_error_from_libra(err); return 0; }
  return 1;
}

const char *shader_chain_last_error(void)
{
  return last_error;
}
