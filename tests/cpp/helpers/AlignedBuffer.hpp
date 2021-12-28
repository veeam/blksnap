#include <stdlib.h>

template <class T>
class AlignedBuffer
{
public:
    AlignedBuffer(size_t size)
        : m_alignment(size*sizeof(T))
        , m_size(size)
    {
        Allocate();
    };
    AlignedBuffer(size_t alignment, size_t size)
        : m_alignment(size)
        , m_size(size)
    {
        Allocate();

    };
    ~AlignedBuffer()
    {
        Free();
    };

    void Resize(size_t size)
    {
        Free();
        m_size = size;
        Allocate();
    };

    size_t Size()
    {
        return m_size;
    }

    T *Data()
    {
        return static_cast<T *>(m_buf);
    };

private:
    size_t m_alignment;
    size_t m_size;
    void *m_buf;

private:
    void Free()
    {
        if (m_buf) {
            ::free(m_buf);
            m_buf = nullptr;
        }
    };
    void Allocate()
    {
        m_buf = ::aligned_alloc(m_alignment, m_size*sizeof(T));
    };
};
