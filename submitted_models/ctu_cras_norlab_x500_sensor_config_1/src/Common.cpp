/* includes //{ */

#include <Common.h>

#include <string>

#include <ignition/common/Console.hh>
#include <ignition/math/eigen3/Conversions.hh>

#include <ignition/gazebo/components/ChildLinkName.hh>
#include <ignition/gazebo/components/Joint.hh>
#include <ignition/gazebo/components/JointAxis.hh>
#include <ignition/gazebo/components/Link.hh>
#include <ignition/gazebo/components/Name.hh>
#include <ignition/gazebo/components/ParentEntity.hh>
#include <ignition/gazebo/components/Pose.hh>
#include "ignition/gazebo/components/LinearVelocity.hh"
#include "ignition/gazebo/components/AngularVelocity.hh"

//}

namespace ignition
{
namespace gazebo
{
inline namespace IGNITION_GAZEBO_VERSION_NAMESPACE
{
namespace systems
{
namespace multicopter_control
{

/* loadRotorConfiguration() //{ */

RotorConfiguration loadRotorConfiguration(const EntityComponentManager &_ecm, const sdf::ElementPtr &_sdf, const Model &_model, const Entity &_comLink) {

  RotorConfiguration out;
  std::size_t        count = 0;

  for (sdf::ElementPtr elem = _sdf->GetFirstElement(); elem; elem = elem->GetNextElement("rotor"), ++count) {

    Rotor rotor;

    if (!elem->HasElement("jointName")) {
      ignerr << "Please specify jointName for rotor index " << count << std::endl;
      continue;
    }

    const std::string jointName = elem->Get<std::string>("jointName");

    const Entity joint = _ecm.EntityByComponents(components::Joint(), components::Name(jointName), components::ParentEntity(_model.Entity()));

    if (kNullEntity == joint) {
      ignerr << "Joint with name " << jointName << " could not be found in "
             << "model " << _model.Name(_ecm) << " while processing rotor "
             << "index " << count << std::endl;
      continue;
    }

    // Calculate the armLength and angle of propeller. The angle of a
    // propeller is the angle about the z axis from the x axis of the frame
    // attached to _comLink to the joint.

    // First, get the pose of the joint w.r.t _comLink
    const std::string childLinkName = _ecm.Component<components::ChildLinkName>(joint)->Data();

    const Entity childLink = _ecm.EntityByComponents(components::Link(), components::Name(childLinkName), components::ParentEntity(_model.Entity()));

    if (kNullEntity == childLink) {
      ignerr << "Child link of joint " << jointName << " with name " << childLinkName << " could not be found in  model " << _model.Name(_ecm)
             << " while processing rotor index " << count << std::endl;
      continue;
    }

    const math::Pose3d jointPose     = _ecm.Component<components::Pose>(joint)->Data();
    const math::Pose3d childLinkPose = _ecm.Component<components::Pose>(childLink)->Data();
    const math::Pose3d comLinkPose   = _ecm.Component<components::Pose>(_comLink)->Data();

    const math::Pose3d jointPoseComLink = comLinkPose.Inverse() * (childLinkPose * jointPose);

    // Project jointPoseComLink into the rotor plane, which is assumed to be
    // the XY plane.
    math::Vector3d unitZ     = math::Vector3d::UnitZ;
    math::Vector3d rotorProj = jointPoseComLink.Pos() - (unitZ.Dot(jointPoseComLink.Pos()) * unitZ);

    rotor.armLength = rotorProj.Length();
    math::Quaterniond rot;
    rot.From2Axes(math::Vector3d::UnitX, rotorProj);
    rotor.angle = rot.Yaw();

    if (!elem->HasElement("forceConstant")) {
      ignerr << "Please specify forceConstant for rotor index " << count << std::endl;
      continue;
    }
    rotor.forceConstant = elem->Get<double>("forceConstant");

    // forceConstant assumes that the rotor's thrust is along the body's +z
    // direction. However, UAVs have their rotors oriented such they are not
    // orthogonal to the rotor plane (the COM link frame's xy plane). To
    // account for the difference in thrust, we multiply the forceConstant
    // with the projection of the rotor's vector onto the +z axis of the
    // COM link frame.
    auto axis = _ecm.Component<components::JointAxis>(joint);
    if (nullptr != axis) {
      math::Vector3d xyzInJoint;
      axis->Data().ResolveXyz(xyzInJoint);

      auto xyzInComLink = comLinkPose.Rot().Inverse() * (childLinkPose.Rot() * jointPose.Rot() * xyzInJoint);

      // The projection onto the +z axis is just the z component of the
      // xyzInComLink vector
      rotor.forceConstant *= xyzInComLink.Z();
    }

    if (!elem->HasElement("momentConstant")) {
      ignerr << "Please specify momentConstant for rotor index " << count << std::endl;
      continue;
    }

    rotor.momentConstant = elem->Get<double>("momentConstant");

    if (!elem->HasElement("direction")) {
      ignerr << "Please specify direction for rotor index " << count << std::endl;
      continue;
    }

    rotor.direction = elem->Get<int>("direction");

    out.push_back(rotor);
  }

  return out;
}

//}

/* calculateAllocationMatrix() //{ */

std::optional<Eigen::Matrix4Xd> calculateAllocationMatrix(const RotorConfiguration &_rotorConfiguration) {

  Eigen::Matrix4Xd allocationMatrix;

  allocationMatrix.resize(4, _rotorConfiguration.size());

  unsigned int i = 0;

  for (const Rotor &rotor : _rotorConfiguration) {

    // Set first row of allocation matrix.
    allocationMatrix(0, i) = sin(rotor.angle) * rotor.armLength * rotor.forceConstant;
    // Set second row of allocation matrix.
    allocationMatrix(1, i) = -cos(rotor.angle) * rotor.armLength * rotor.forceConstant;
    // Set third row of allocation matrix.
    allocationMatrix(2, i) = -rotor.direction * rotor.forceConstant * rotor.momentConstant;
    // Set forth row of allocation matrix.
    allocationMatrix(3, i) = rotor.forceConstant;
    ++i;
  }

  Eigen::FullPivLU<Eigen::Matrix4Xd> lu(allocationMatrix);

  lu.setThreshold(1e-9);
  int rank = lu.rank();

  if (rank < 4) {
    ignerr << "The rank of the allocation matrix is " << lu.rank() << ", it should have rank 4, to have a fully controllable system,"
           << " check your configuration." << std::endl;
    return std::nullopt;
  }

  return std::make_optional(allocationMatrix);
}

//}

/* createFrameDataComponents() //{ */

void createFrameDataComponents(EntityComponentManager &_ecm, const Entity &_link) {

  // Create a world Pose component if one is not present
  if (_ecm.Component<components::WorldPose>(_link)) {
    _ecm.CreateComponent(_link, components::WorldPose());
  }

  // Create a world linear velocity component if one is not present.
  if (!_ecm.Component<components::WorldLinearVelocity>(_link)) {
    _ecm.CreateComponent(_link, components::WorldLinearVelocity());
  }

  // Create an angular velocity component if one is not present.
  if (!_ecm.Component<components::AngularVelocity>(_link)) {
    _ecm.CreateComponent(_link, components::AngularVelocity());
  }
}

//}

/* getFrameData() //{ */

std::optional<FrameData> getFrameData(const EntityComponentManager &_ecm, const Entity &_link, const NoiseParameters &_noise) {

  auto poseComp = _ecm.Component<components::WorldPose>(_link);

  if (!poseComp) {
    ignerr << "WorldPose component not found on link entity " << _link << std::endl;
    return std::nullopt;
  }

  auto velComp = _ecm.Component<components::WorldLinearVelocity>(_link);

  if (!velComp) {
    ignerr << "WorldLinearVelocity component not found on link entity " << _link << std::endl;
    return std::nullopt;
  }
  auto angVelComp = _ecm.Component<components::AngularVelocity>(_link);

  if (!angVelComp) {
    ignerr << "AngularVelocity component not found on link entity " << _link << std::endl;
    return std::nullopt;
  }

  auto applyNoise = [](Eigen::Vector3d &_val, const Eigen::Vector3d &_mean, const Eigen::Vector3d &_stdDev) {
    for (int i = 0; i < 3; ++i) {
      if (_stdDev(i) > 0) {
        _val(i) += math::Rand::DblNormal(_mean(i), _stdDev(i));
      }
    }
  };

  auto frameData = std::make_optional<FrameData>(
      {math::eigen3::convert(poseComp->Data()), math::eigen3::convert(velComp->Data()), math::eigen3::convert(angVelComp->Data())});

  // --------------------------------------------------------------
  // |         add noise to the ideal simulated UAV states        |
  // --------------------------------------------------------------
  applyNoise(frameData->linearVelocityWorld, _noise.linearVelocityMean, _noise.linearVelocityStdDev);
  applyNoise(frameData->angularVelocityBody, _noise.angularVelocityMean, _noise.angularVelocityStdDev);

  return frameData;
}

//}

}  // namespace multicopter_control
}  // namespace systems
}  // namespace IGNITION_GAZEBO_VERSION_NAMESPACE
}  // namespace gazebo
}  // namespace ignition
