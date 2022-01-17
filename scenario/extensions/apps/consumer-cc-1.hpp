// #ifndef NDN_CONSUMER_CBR_H
// #define NDN_CONSUMER_CBR_H

#include "ns3/ndnSIM/model/ndn-common.hpp"

// #include "ndn-consumer.hpp"
# include "../../../ns-3/src/ndnSIM/apps/ndn-consumer.hpp"

namespace ns3 {
namespace ndn {

class ConsumerCC1 : public Consumer {
  public:
    static TypeId GetTypeId();
	
    ConsumerCC1();
    virtual ~ConsumerCC1();

  protected:
    virtual void ScheduleNextPacket();
    void SetRandomize(const std::string& value);
    std::string GetRandomize() const;

  protected:
    double m_frequency;                 // Frequency of interest packets (in hertz)
    bool m_firstTime;                   // 标志是不是第一次请求
    Ptr<RandomVariableStream> m_random; // 随机数生成器
    std::string m_randomType;           // 随机的规则?
};

} // namespace ndn
} // namespace ns3

// #endif
