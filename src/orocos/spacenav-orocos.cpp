#include "spacenav-orocos.hpp"
#include <rtt/extras/FileDescriptorActivity.hpp>

using namespace cosima::hw;

SpaceNavOrocos::SpaceNavOrocos(std::string const &name) : RTT::TaskContext(name), offsetTranslation(0.001), offsetOrientation(0.001), button1_old(false), button2_old(false), enableX(true), enableY(true), enableZ(true), enableA(true), enableB(true), enableC(true)
{
    addOperation("displayStatus", &SpaceNavOrocos::displayStatus, this).doc("Display the current status of this component.");

    addProperty("offsetTranslation", offsetTranslation);
    addProperty("offsetOrientation", offsetOrientation);
    addProperty("enableX", enableX);
    addProperty("enableY", enableY);
    addProperty("enableZ", enableZ);
    addProperty("enableA", enableA);
    addProperty("enableB", enableB);
    addProperty("enableC", enableC);
    interface = new SpaceNavHID();
}

// SpaceNavOrocos::~SpaceNavOrocos() {
//     if (interface) {
//         delete interface;
//     }
// }

bool SpaceNavOrocos::configureHook()
{
    if (!interface->init())
    {
        RTT::log(RTT::Error) << "[" << this->getName() << "] "
                             << "Unable to access Space Nav at " << getFileDescriptor() << RTT::endlog();
        return false;
    }

    if (this->getPort("out_6d_port"))
    {
        this->ports()->removePort("out_6d_port");
    }
    out_6d_var = Eigen::VectorXf(interface->getNumAxes());
    out_6d_var.setZero();
    out_6d_port.setName("out_6d_port");
    out_6d_port.doc("Output port for 6D command vector");
    out_6d_port.setDataSample(out_6d_var);
    ports()->addPort(out_6d_port);

    if (this->getPort("out_pose_port"))
    {
        this->ports()->removePort("out_pose_port");
    }
    out_pose_var = rstrt::geometry::Pose();
    out_pose_port.setName("out_pose_port");
    out_pose_port.doc("Output port for pose command vector");
    out_pose_port.setDataSample(out_pose_var);
    ports()->addPort(out_pose_port);

    if (this->getPort("in_current_pose_port"))
    {
        this->ports()->removePort("in_current_pose_port");
    }
    in_current_pose_var = rstrt::geometry::Pose();
    in_current_pose_port.setName("in_current_pose_port");
    in_current_pose_port.doc("Input port for the current pose to which the commands should be added.");
    ports()->addPort(in_current_pose_port);
    in_current_pose_flow = RTT::NoData;

    values.reset();
    rawValues.reset();

    // indicate proper setup by flashing the led!
    for (uint i = 0; i < 3; i++)
    {
        interface->setLedState(1);
        usleep(100000);
        interface->setLedState(0);
        usleep(100000);
    }

    return true;
}

int SpaceNavOrocos::getFileDescriptor()
{
    return interface->getFileDescriptor();
}

bool SpaceNavOrocos::startHook()
{
    RTT::extras::FileDescriptorActivity *activity = getActivity<RTT::extras::FileDescriptorActivity>();
    if (activity)
    {
        activity->watch(getFileDescriptor());
        // get trigger a least every 25 ms
        // activity->setTimeout(25);
        interface->setLedState(1);
        return true;
    }
    return false;
}

// void SpaceNavOrocos::errorHook() {
//     RTT::log(RTT::Fatal) << "ERROR??? TODO" << RTT::endlog();
// }

template <typename T>
int sgn(T val)
{
    return (T(0) < val) - (val < T(0));
}

void SpaceNavOrocos::updateHook()
{
    interface->getValue(values, rawValues);

    // TODO do some scaling!
    if (values.button1 != button1_old)
    {
        button1_old = values.button1;
        if (!button1_old)
        {
            RTT::log(RTT::Error) << "[" << this->getName() << "] "
                                 << "Enabled translation change." << RTT::endlog();
        }
        else
        {
            RTT::log(RTT::Error) << "[" << this->getName() << "] "
                                 << "Disabled translation change." << RTT::endlog();
        }
    }

    if (values.button2 != button2_old)
    {
        button2_old = values.button2;
        if (!button2_old)
        {
            RTT::log(RTT::Error) << "[" << this->getName() << "] "
                                 << "Enabled orientation change." << RTT::endlog();
        }
        else
        {
            RTT::log(RTT::Error) << "[" << this->getName() << "] "
                                 << "Disabled orientation change." << RTT::endlog();
        }
    }

    if (!values.button1)
    {
        out_6d_var(0) = enableX ? sgn(values.tx) * offsetTranslation : 0.0;
        out_6d_var(1) = enableY ? sgn(values.ty) * offsetTranslation : 0.0;
        out_6d_var(2) = enableZ ? sgn(values.tz) * offsetTranslation : 0.0;
    }
    else
    {
        out_6d_var(0) = 0;
        out_6d_var(1) = 0;
        out_6d_var(2) = 0;
    }

    if (!values.button2)
    {
        out_6d_var(3) = enableA ? sgn(values.rx) * offsetOrientation : 0.0;
        out_6d_var(4) = enableB ? sgn(values.ry) * offsetOrientation : 0.0;
        out_6d_var(5) = enableC ? sgn(values.rz) * offsetOrientation : 0.0;
    }
    else
    {
        out_6d_var(3) = 0;
        out_6d_var(4) = 0;
        out_6d_var(5) = 0;
    }

    if (!in_current_pose_port.connected())
    {
        // if we do not have a pose to add stuff to, we just return the stuff...
        out_6d_port.write(out_6d_var);
    }
    else
    {
        // if we do have a pose, we treat our values as new delta!
        if (in_current_pose_flow == RTT::NoData)
        {
            // get the ground truth only once!
            in_current_pose_flow = in_current_pose_port.read(in_current_pose_var);
            return;
        }

        Eigen::AngleAxisf rollAngle(out_6d_var(3), Eigen::Vector3f::UnitX());
        Eigen::AngleAxisf pitchAngle(out_6d_var(4), Eigen::Vector3f::UnitY());
        Eigen::AngleAxisf yawAngle(out_6d_var(5), Eigen::Vector3f::UnitZ());
        Eigen::Quaternionf q = rollAngle * pitchAngle * yawAngle;

        Eigen::Quaternionf qBase = Eigen::Quaternionf(in_current_pose_var.rotation.rotation(0), in_current_pose_var.rotation.rotation(1), in_current_pose_var.rotation.rotation(2), in_current_pose_var.rotation.rotation(3));
        qBase *= q;

        out_pose_var.translation.translation(0) = in_current_pose_var.translation.translation(0) + out_6d_var(0);
        out_pose_var.translation.translation(1) = in_current_pose_var.translation.translation(1) + out_6d_var(1);
        out_pose_var.translation.translation(2) = in_current_pose_var.translation.translation(2) + out_6d_var(2);
        out_pose_var.rotation.rotation(0) = qBase.w();
        out_pose_var.rotation.rotation(1) = qBase.x();
        out_pose_var.rotation.rotation(2) = qBase.y();
        out_pose_var.rotation.rotation(3) = qBase.z();

        // out_pose_var.rotation.rotation(0) = 0;
        // out_pose_var.rotation.rotation(1) = 0;
        // out_pose_var.rotation.rotation(2) = 1;
        // out_pose_var.rotation.rotation(3) = 0;

        // save state to not return to the initially read pose.
        in_current_pose_var = out_pose_var;
        out_pose_port.write(out_pose_var);
    }

    // if (interface->getNumAxes() != axisoffsetTranslations.size()) {
    //     // TODO send zero or just do not offsetTranslation ??
    //     return;
    // }

    // for(size_t i = 0 ; i < axisoffsetTranslations.size(); i++)
    // {
    //     roffsetTranslationd.axisValue[i] *= axisoffsetTranslations[i];
    // }
}

void SpaceNavOrocos::stopHook()
{
    RTT::extras::FileDescriptorActivity *activity = getActivity<RTT::extras::FileDescriptorActivity>();
    if (activity)
        activity->clearAllWatches();
    interface->setLedState(0);
}

void SpaceNavOrocos::cleanupHook()
{
    if (interface)
    {
        delete interface;
    }
}

void SpaceNavOrocos::displayStatus()
{
    RTT::log(RTT::Error) << "[" << this->getName() << "] Info\n"
                         << "Listening to interface " << getFileDescriptor() << "\n"
                         << "Button 1 " << (!button1_old ? "Not pressed => Translation enabled" : "Pressed => Translation disabled") << "\n"
                         << "Button 2 " << (!button2_old ? "Not pressed => Orientation enabled" : "Pressed => Orientation disabled") << "\n"
                         << "enableX = " << enableX << "\n"
                         << "enableY = " << enableY << "\n"
                         << "enableZ = " << enableZ << "\n"
                         << "enableA = " << enableA << "\n"
                         << "enableB = " << enableB << "\n"
                         << "enableC = " << enableC << "\n"
                         << RTT::endlog();
}

ORO_CREATE_COMPONENT_LIBRARY()
ORO_LIST_COMPONENT_TYPE(cosima::hw::SpaceNavOrocos)