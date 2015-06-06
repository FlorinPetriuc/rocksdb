#ifdef ROCKSDB_PLATFORM_NVM

#include "nvm/nvm.h"

list_node::list_node(void *_data)
{
    data = _data;

    next = nullptr;
    prev = nullptr;
}

list_node::~list_node()
{
}

list_node *list_node::GetNext()
{
    return next;
}

list_node *list_node::GetPrev()
{
    return prev;
}

void *list_node::GetData()
{
    return data;
}

void *list_node::SetData(void *_data)
{
    void *ret = data;

    data = _data;

    return ret;
}

void *list_node::SetNext(list_node *_next)
{
    void *ret = next;

    next = _next;

    return ret;
}

void *list_node::SetPrev(list_node *_prev)
{
    void *ret = prev;

    prev = _prev;

    return ret;
}

nvm::nvm()
{
    fd = open_nvm_device("/rocksdb");

    if(fd < 0)
    {
	NVM_FATAL("");
    }

    if(ioctl_initialize())
    {
	NVM_FATAL("");
    }
}

nvm::~nvm()
{
    unsigned long i;
    unsigned long j;

    close(fd);

    if(nr_luns > 0)
    {
	for(i = 0; i < nr_luns; ++i)
	{
	    for(j = 0; j < luns[i].nr_blocks; ++j)
	    {
		free(luns[i].blocks[j].block);
		free(luns[i].blocks[j].pages);
	    }
	    free(luns[i].blocks);
	    free(luns[i].channels);
	}
	free(luns);
    }

    NVM_DEBUG("api closed");
}

int nvm::open_nvm_device(const char *file)
{
    std::string cmd = std::string("echo \"nba ") + std::string(file) +
		      std::string(" 0:0\" > /sys/block/nvme0n1/lightnvm/configure");

    if(system(cmd.c_str()))
    {
	return -1;
    }

    location = std::string("/dev") + std::string(file);

    return open(location.c_str(), O_RDWR);
}

const char *nvm::GetLocation()
{
    return location.c_str();
}

void nvm::ReclaimPage(struct nvm_page *page)
{
    page->allocated = false;
}

int nvm::ioctl_initialize()
{
    int ret;

    unsigned long i;
    unsigned long j;
    unsigned long k;
    unsigned long l;

    struct nba_channel chnl_desc;

    ret = ioctl(fd, NVMLUNSNRGET, &nr_luns);

    if(ret != 0)
    {
	NVM_ERROR("%d", ret);

	goto err;
    }

    NVM_DEBUG("We have %lu luns", nr_luns);

    ALLOC_STRUCT(luns, nr_luns, struct nvm_lun);

    for(i = 0; i < nr_luns; ++i)
    {
	luns[i].nr_pages_per_blk = i;

	ret = ioctl(fd, NVMPAGESNRGET, &luns[i].nr_pages_per_blk);

	if(ret != 0)
	{
	    NVM_ERROR("%d", ret);

	    goto err;
	}

	NVM_DEBUG("Lun %lu has %lu pages per block", i, luns[i].nr_pages_per_blk);

	luns[i].nchannels = i;

	ret = ioctl(fd, NVMCHANNELSNRGET, &luns[i].nchannels);

	if(ret != 0)
	{
	    NVM_ERROR("%d", ret);

	    goto err;
	}

	NVM_DEBUG("Lun %lu has %lu channels", i, luns[i].nchannels);

	ALLOC_STRUCT(luns[i].channels, luns[i].nchannels, struct nvm_channel);

	chnl_desc.lun_idx = i;

	for(j = 0; j < luns[i].nchannels; ++j)
	{
	    chnl_desc.chnl_idx = j;

	    ret = ioctl(fd, NVMPAGESIZEGET, &chnl_desc);

	    if(ret != 0)
	    {
		NVM_ERROR("%d", ret);

		goto err;
	    }

	    luns[i].channels[j].gran_erase = chnl_desc.gran_erase;
	    luns[i].channels[j].gran_read = chnl_desc.gran_read;
	    luns[i].channels[j].gran_write = chnl_desc.gran_write;

	    NVM_DEBUG("Lun %lu channel %lu has %u writes %u reads and %u erase", i, luns[i].nchannels,
										    luns[i].channels[j].gran_write,
										    luns[i].channels[j].gran_read,
										    luns[i].channels[j].gran_erase);
	}

	luns[i].nr_blocks = i;

	ret = ioctl(fd, NVMBLOCKSNRGET, &luns[i].nr_blocks);

	if(ret != 0)
	{
	    NVM_ERROR("%d", ret);

	    goto err;
	}

	NVM_DEBUG("Lun %lu has %lu blocks", i, luns[i].nr_blocks);

	ALLOC_STRUCT(luns[i].blocks, luns[i].nr_blocks, struct nvm_block);

	for(j = 0; j < luns[i].nr_blocks; ++j)
	{
	    struct nba_block *blk;
	    struct nvm_block *process_blk = &luns[i].blocks[j];

	    ALLOC_STRUCT(blk, 1, struct nba_block);

	    blk->id = j;
	    blk->lun = i;

	    ret = ioctl(fd, NVMBLOCKGETBYID, blk);

	    if(ret)
	    {
		NVM_ERROR("%d", ret);

		goto err;
	    }

	    process_blk->block = blk;
	    process_blk->has_stale_pages = true;

	    ALLOC_STRUCT(process_blk->pages, luns[i].nr_pages_per_blk, struct nvm_page);

	    for(k = 0; k < luns[i].nr_pages_per_blk; ++k)
	    {
		process_blk->pages[k].lun_id = i;
		process_blk->pages[k].block_id = j;
		process_blk->pages[k].id = k;

		process_blk->pages[k].allocated = false;
		process_blk->pages[k].erased = false;

		process_blk->pages[k].sizes_no = luns[i].nchannels;
		SAFE_MALLOC(process_blk->pages[k].sizes, luns[i].nchannels, unsigned int)

		for(l = 0; l < process_blk->pages[k].sizes_no; ++l)
		{
		    process_blk->pages[k].sizes[l] = luns[i].channels[l].gran_write;
		}
	    }
	}
    }

    return 0;

err:
    return 1;
}

#endif