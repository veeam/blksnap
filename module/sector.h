#pragma once

static inline unsigned int sector_to_uint( sector_t sect )
{
    return (unsigned int)(sect << SECTOR_SHIFT);
}

static inline size_t sector_to_size( sector_t sect )
{
    return (size_t)(sect << SECTOR_SHIFT);
}

static inline u64 sector_to_streamsize( sector_t sect )
{
    return (u64)(sect) << (u64)(SECTOR_SHIFT);
}

static inline sector_t sector_from_uint( unsigned int size )
{
    return (sector_t)(size >> SECTOR_SHIFT);
}

static inline sector_t sector_from_size( size_t size )
{
    return (sector_t)(size >> SECTOR_SHIFT);
}

static inline sector_t sector_from_streamsize( u64 steamsize )
{
    return (sector_t)(steamsize >> SECTOR_SHIFT);
}
