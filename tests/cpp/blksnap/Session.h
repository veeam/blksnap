/*
 * The hi-level abstraction for the blksnap kernel module.
 * Allows to create snapshot session.
 */
#include <vector>
#include <string>
#include <memory>

namespace blksnap
{

struct ISession
{
    virtual ~ISession() {};

    virtual std::string GetImageDevice(const std::string& original) = 0;
    virtual std::string GetOriginalDevice(const std::string& image) = 0;
    virtual bool GetError(std::string &errorMessage) = 0;

    //TODO: add limits
    static std::shared_ptr<ISession> Create(
        const std::vector<std::string>& devices, const std::string &diffStorage);
};

}
