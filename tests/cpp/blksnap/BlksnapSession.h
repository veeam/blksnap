#include <vector>
#include <string>
#include <memory>

struct IBlksnapSession
{
    //virtual ~IBlksnapSession() = 0;

    virtual std::string GetImageDevice(const std::string& original) = 0;
};

std::shared_ptr<IBlksnapSession>
CreateBlksnapSession(const std::vector<std::string>& devices, const std::string &diffStorage);
