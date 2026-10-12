/*
 * droptarget.c — ファイルのドラッグ＆ドロップを受け付ける OLE ドロップターゲット
 *
 * RichEdit が登録する独自のドロップターゲットは、読み取り専用ではファイルを拒否する。
 * そのため RevokeDragDrop で外してから、CF_HDROP を受け付けるこのターゲットを登録する。
 * オブジェクトは静的に1つだけ持ち、参照カウントで解放しない。
 */
#include "droptarget.h"

#include <ole2.h>
#include <shellapi.h>
#include <stdlib.h>

typedef struct {
    IDropTarget iface;
    LONG ref;
    BOOL accept;  /* DragEnter で CF_HDROP を持っていたか */
    HWND notify;  /* WM_APP_OPENFILE の送り先 */
} DropTarget;

static FORMATETC g_hdropFormat = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

static DWORD drop_effect(DWORD allowed)
{
    if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
    return DROPEFFECT_NONE; /* MOVE は返さない：ドラッグ元が元のファイルを消してしまう */
}

static HRESULT STDMETHODCALLTYPE dt_QueryInterface(IDropTarget *This, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *ppv = This;
        IDropTarget_AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dt_AddRef(IDropTarget *This)
{
    return (ULONG)InterlockedIncrement(&((DropTarget *)This)->ref);
}

static ULONG STDMETHODCALLTYPE dt_Release(IDropTarget *This)
{
    return (ULONG)InterlockedDecrement(&((DropTarget *)This)->ref); /* 静的オブジェクト */
}

static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    DropTarget *dt = (DropTarget *)This;
    dt->accept = data && IDataObject_QueryGetData(data, &g_hdropFormat) == S_OK;
    *effect = dt->accept ? drop_effect(*effect) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_DragOver(IDropTarget *This, DWORD keys, POINTL pt, DWORD *effect)
{
    *effect = ((DropTarget *)This)->accept ? drop_effect(*effect) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_DragLeave(IDropTarget *This)
{
    ((DropTarget *)This)->accept = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    DropTarget *dt = (DropTarget *)This;
    STGMEDIUM medium;
    *effect = DROPEFFECT_NONE;
    if (dt->accept && data && IDataObject_GetData(data, &g_hdropFormat, &medium) == S_OK) {
        HDROP drop = (HDROP)GlobalLock(medium.hGlobal);
        if (drop) {
            /* 複数ドロップされても先頭だけを扱う */
            UINT n = DragQueryFileW(drop, 0, NULL, 0);
            if (n > 0) {
                wchar_t *path = calloc((size_t)n + 1, sizeof(wchar_t));
                if (path && DragQueryFileW(drop, 0, path, n + 1)) {
                    /* 開くのはドロップ処理を返した後（エクスプローラーをダイアログで止めない） */
                    if (!PostMessageW(dt->notify, WM_APP_OPENFILE, 0, (LPARAM)path)) free(path);
                    *effect = drop_effect(DROPEFFECT_COPY | DROPEFFECT_LINK);
                } else {
                    free(path);
                }
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    dt->accept = FALSE;
    return S_OK;
}

static IDropTargetVtbl g_vtable = {
    dt_QueryInterface, dt_AddRef, dt_Release,
    dt_DragEnter, dt_DragOver, dt_DragLeave, dt_Drop,
};
static DropTarget g_target = { { &g_vtable }, 1, FALSE, NULL };

bool droptarget_register(HWND window, HWND notify)
{
    g_target.notify = notify;
    RevokeDragDrop(window);
    return SUCCEEDED(RegisterDragDrop(window, &g_target.iface));
}

void droptarget_revoke(HWND window)
{
    RevokeDragDrop(window);
}
