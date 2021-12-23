
struct IBlksnap
{
    virtual ~IBlksnap() = 0;

    virtual void AppendDiffStorage(const std::string& filename) = 0;
    virtual void Take() = 0;
    virtual std::string GetImageDevice(const std::string& original) = 0;
};

std::shared_ptr<IBlksnap> CreateBlksnap(const std::vector<std::string>& devices);
