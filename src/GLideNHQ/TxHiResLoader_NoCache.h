#ifndef TXHIRESLOADER_NOCACHE_H
#define TXHIRESLOADER_NOCACHE_H

#include "TxHiResLoader.h"
#include "TxHiResCache.h"

typedef struct {
    char fname[MAX_PATH];
    tx_wstring directory;
    uint32 siz;
    uint32 fmt;
} fileIndexEntry_CacheNoCache;

class TxHiResLoader_NoCache : public TxHiResCache
{
private:
    tx_wstring _ident;
    char _identc[MAX_PATH];
    bool _createFileIndex(bool update);
    bool _createFileIndexInDir(tx_wstring directory, bool update, char* identc);
    void _clear();
public:
    std::map<uint64, fileIndexEntry_CacheNoCache> _filesIndex;
    std::map<uint64, GHQTexInfo> _loadedTex;

    ~TxHiResLoader_NoCache();
    TxHiResLoader_NoCache(int maxwidth, int maxheight, int maxbpp, int options,
				const wchar_t* cachePath, const wchar_t* texPackPath, const wchar_t* ident,
				dispInfoFuncExt callback);
    bool empty() const override;
    bool get(Checksum checksum, GHQTexInfo* info) override;
    bool reload() override;
};

extern void AddTexturePath(tx_wstring path);
extern void RemoveTexturePath(tx_wstring path);
extern std::vector<tx_wstring> texture_paths;

#endif
