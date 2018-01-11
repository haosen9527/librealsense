// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#include "environment.h"
#include "device.h"

using namespace librealsense;

std::shared_ptr<matcher> matcher_factory::create(rs2_matchers matcher, std::vector<stream_interface*> profiles)
{
    switch (matcher)
    {
    case DI:
        if (profiles.size() < 2)
        {
            LOG_DEBUG("Created default matcher");
            return create_default_matcher(profiles);
        }
        return create_DI_matcher(profiles);
    case DI_C:
        if (profiles.size() < 3)
        {
            LOG_DEBUG("Created default matcher");
            return create_default_matcher(profiles);
        }
        return create_DI_C_matcher(profiles);
    case DLR_C:
        if (profiles.size() < 4)
        {
            LOG_DEBUG("Created default matcher");
            return create_default_matcher(profiles);
        }
        return create_DLR_C_matcher(profiles);
    case DLR:
        if (profiles.size() < 3)
        {
            LOG_DEBUG("Created default matcher");
            return create_default_matcher(profiles);
        }
        return create_DLR_matcher(profiles);
    case DEFAULT:default:
        LOG_DEBUG("Created default matcher");
        return create_default_matcher(profiles);
        break;
    }
}
std::shared_ptr<matcher> matcher_factory::create_DLR_C_matcher(std::vector<stream_interface*> profiles)
{
    return create_timestamp_composite_matcher({ create_DLR_matcher({ profiles[0], profiles[1], profiles[2] }),
        create_identity_matcher(profiles[3]) });
}
std::shared_ptr<matcher> matcher_factory::create_DLR_matcher(std::vector<stream_interface*> profiles)
{
    std::vector<std::shared_ptr<matcher>> depth_matchers;

    for (auto& p : profiles)
        depth_matchers.push_back(std::make_shared<identity_matcher>(p->get_unique_id(), p->get_stream_type()));

    return std::make_shared<frame_number_composite_matcher>(depth_matchers);
}

std::shared_ptr<matcher> matcher_factory::create_DI_C_matcher(std::vector<stream_interface*> profiles)
{
    return create_timestamp_composite_matcher({ create_DI_matcher({ profiles[0], profiles[1] }),
        create_identity_matcher(profiles[2]) });
}

std::shared_ptr<matcher> matcher_factory::create_DI_matcher(std::vector<stream_interface*> profiles)
{
    std::vector<std::shared_ptr<matcher>> depth_matchers;

    for (auto& p : profiles)
        depth_matchers.push_back(std::make_shared<identity_matcher>(p->get_unique_id(), p->get_stream_type()));

    return std::make_shared<frame_number_composite_matcher>(depth_matchers);
}

std::shared_ptr<matcher> matcher_factory::create_default_matcher(std::vector<stream_interface*> profiles)
{
    std::vector<std::shared_ptr<matcher>> matchers;
    for (auto& p : profiles)
        matchers.push_back(std::make_shared<identity_matcher>(p->get_unique_id(), p->get_stream_type()));

    return create_timestamp_composite_matcher(matchers);
}
std::shared_ptr<matcher> matcher_factory::create_identity_matcher(stream_interface *profile)
{
    return std::make_shared<identity_matcher>(profile->get_unique_id(), profile->get_stream_type());
}
std::shared_ptr<matcher> matcher_factory::create_timestamp_composite_matcher(std::vector<std::shared_ptr<matcher>> matchers)
{
    return std::make_shared<timestamp_composite_matcher>(matchers);
}

device::device(std::shared_ptr<context> ctx,
               const platform::backend_device_group group,
               bool device_changed_notifications)
    : _context(ctx), _group(group), _is_valid(true),
      _device_changed_notifications(device_changed_notifications)
{
    if (_device_changed_notifications)
    {
        auto cb = new devices_changed_callback_internal([this](rs2_device_list* removed, rs2_device_list* added)
        {
            // Update is_valid variable when device is invalid
            std::lock_guard<std::mutex> lock(_device_changed_mtx);
            for (auto& dev_info : removed->list)
            {
                if (dev_info.info->get_device_data() == _group)
                {
                    _is_valid = false;
                    return;
                }
            }
        });

        _callback_id = _context->register_internal_device_callback({ cb, [](rs2_devices_changed_callback* p) { p->release(); } });
    }
}

device::~device()
{
    if (_device_changed_notifications)
    {
        _context->unregister_internal_device_callback(_callback_id);
    }
    _sensors.clear();
}

int device::add_sensor(std::shared_ptr<sensor_interface> sensor_base)
{
    _sensors.push_back(sensor_base);
    return (int)_sensors.size() - 1;
}

int device::assign_sensor(std::shared_ptr<sensor_interface> sensor_base, uint8_t idx)
{
    try
    {
        _sensors[idx] = sensor_base;
        return (int)_sensors.size() - 1;
    }
    catch (std::out_of_range)
    {
        throw invalid_value_exception(to_string() << "Cannot assign sensor - invalid subdevice value" << idx);
    }
}

uvc_sensor& device::get_uvc_sensor(int sub)
{
    return dynamic_cast<uvc_sensor&>(*_sensors[sub]);
}

size_t device::get_sensors_count() const
{
    return static_cast<unsigned int>(_sensors.size());
}

sensor_interface& device::get_sensor(size_t subdevice)
{
    try
    {
        return *(_sensors.at(subdevice));
    }
    catch (std::out_of_range)
    {
        throw invalid_value_exception("invalid subdevice value");
    }
}

size_t device::find_sensor_idx(const sensor_interface& s) const
{
    int idx = 0;
    for (auto&& sensor : _sensors)
    {
        if (&s == sensor.get()) return idx;
        idx++;
    }
    throw std::runtime_error("Sensor not found!");
}

const sensor_interface& device::get_sensor(size_t subdevice) const
{
    try
    {
        return *(_sensors.at(subdevice));
    }
    catch (std::out_of_range)
    {
        throw invalid_value_exception("invalid subdevice value");
    }
}

void device::hardware_reset()
{
    throw not_implemented_exception(to_string() << __FUNCTION__ << " is not implemented for this device!");
}

std::shared_ptr<matcher> librealsense::device::create_matcher(const frame_holder& frame) const
{

    return std::make_shared<identity_matcher>( frame.frame->get_stream()->get_unique_id(), frame.frame->get_stream()->get_stream_type());
}

std::pair<uint32_t, rs2_extrinsics> librealsense::device::get_extrinsics(const stream_interface& stream) const
{
    auto stream_index = stream.get_unique_id();
    auto pair = _extrinsics.at(stream_index);
    auto pin_stream = pair.second;
    rs2_extrinsics ext{};
    if (environment::get_instance().get_extrinsics_graph().try_fetch_extrinsics(*pin_stream, stream, &ext) == false)
    {
        throw std::runtime_error(to_string() << "Failed to fetch extrinsics between pin stream (" << pin_stream->get_unique_id() << ") to given stream (" << stream.get_unique_id() << ")");
    }
    return std::make_pair(pair.first, ext);
}

void librealsense::device::register_stream_to_extrinsic_group(const stream_interface& stream, uint32_t groupd_index)
{
    auto iter = std::find_if(_extrinsics.begin(),
                           _extrinsics.end(),
                           [groupd_index](const std::pair<int, std::pair<uint32_t, std::shared_ptr<const stream_interface>>>& p) { return p.second.first == groupd_index; });
    if (iter == _extrinsics.end())
    {
        //First stream to register for this group
        _extrinsics[stream.get_unique_id()] = std::make_pair(groupd_index, stream.shared_from_this());
    }
    else
    {
        //iter->second holds the group_id and the key stream
        _extrinsics[stream.get_unique_id()] = iter->second;
    }
}
