/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2021 Fragcolor Pte. Ltd. */

// RigidBody and Collier are unique, Shapes can be shared

use crate::core::registerShard;
use crate::shards::physics::BaseShape;
use crate::shards::physics::Simulation;
use crate::shards::physics::EXPOSED_SIMULATION;
use crate::shards::physics::POSITION_TYPES_SLICE;
use crate::shards::physics::ROTATION_TYPES_SLICE;
use crate::shards::physics::SHAPE_TYPE;
use crate::shards::physics::SHAPE_TYPES;
use crate::shards::physics::SIMULATION_TYPE;
use crate::types::common_type;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::Type;
use crate::types::ANY_TYPES;
use crate::types::FLOAT3_TYPES_SLICE;
use crate::types::FLOAT_TYPES_SLICE;
use crate::types::NONE_TYPES;
use crate::Shard;
use crate::Types;
use crate::Var;
use rapier3d::dynamics::{IntegrationParameters, JointSet, RigidBodySet};
use rapier3d::geometry::SharedShape;
use rapier3d::geometry::{BroadPhase, ColliderSet, ContactEvent, IntersectionEvent, NarrowPhase};
use rapier3d::na::{Isometry3, Quaternion, Translation, UnitQuaternion, Vector3};
use rapier3d::pipeline::{ChannelEventCollector, PhysicsPipeline};
use std::convert::TryInto;

lazy_static! {
  static ref PARAMETERS: Parameters = vec![
    (
      cstr!("Position"),
      shccstr!("The position wrt. the body it is attached to."),
      POSITION_TYPES_SLICE
    )
      .into(),
    (
      cstr!("Rotation"),
      shccstr!("The rotation  wrt. the body it is attached to"),
      ROTATION_TYPES_SLICE
    )
      .into()
  ];
}

macro_rules! shape {
  ($shard_name:ident, $name_str:literal, $hash:literal) => {
    impl Shard for $shard_name {
      fn registerName() -> &'static str {
        cstr!($name_str)
      }

      fn hash() -> u32 {
        compile_time_crc32::crc32!($hash)
      }

      fn name(&mut self) -> &str {
        $name_str
      }

      fn inputTypes(&mut self) -> &Types {
        &NONE_TYPES
      }

      fn outputTypes(&mut self) -> &Types {
        &SHAPE_TYPES
      }

      fn parameters(&mut self) -> Option<&Parameters> {
        self._parameters()
      }

      fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
        match index {
          0 => Ok(self.position.set_param(value)),
          1 => Ok(self.rotation.set_param(value)),
          _ => Ok(self._set_param(index, value)),
        }
      }

      fn getParam(&mut self, index: i32) -> Var {
        match index {
          0 => self.position.get_param(),
          1 => self.rotation.get_param(),
          _ => self._get_param(index),
        }
      }

      fn cleanup(&mut self) -> Result<(), &str> {
        self.bs.shape = None;
        self.position.cleanup();
        self.rotation.cleanup();
        self._cleanup();
        Ok(())
      }

      fn warmup(&mut self, context: &Context) -> Result<(), &str> {
        self.position.warmup(context);
        self.rotation.warmup(context);
        self._warmup(context)
      }

      fn activate(&mut self, _: &Context, _: &Var) -> Result<Var, &str> {
        let pos = {
          let p = self.position.get();
          if p.is_none() {
            Vector3::new(0.0, 0.0, 0.0)
          } else {
            let (tx, ty, tz): (f32, f32, f32) = p.try_into()?;
            Vector3::new(tx, ty, tz)
          }
        };
        self.bs.position = Some({
          let r = self.rotation.get();
          if r.is_none() {
            Isometry3::new(pos, Vector3::new(0.0, 0.0, 0.0))
          } else {
            let quaternion: Result<(f32, f32, f32, f32), &str> = r.try_into();
            if let Ok(quaternion) = quaternion {
              let quaternion =
                Quaternion::new(quaternion.3, quaternion.0, quaternion.1, quaternion.2);
              let quaternion = UnitQuaternion::from_quaternion(quaternion);
              let pos = Translation::from(pos);
              Isometry3::from_parts(pos, quaternion)
            } else {
              // if setParam validation is correct this is impossible
              panic!("unexpected branch")
            }
          }
        });
        self.bs.shape = Some(self.make());
        Ok(unsafe { Var::new_object_from_ptr(&self.bs as *const BaseShape, &SHAPE_TYPE) })
      }
    }
  };
}

lazy_static! {
  static ref BALL_PARAMETERS: Parameters = {
    let mut v = vec![(
      cstr!("Radius"),
      shccstr!("The radius of the sphere."),
      FLOAT_TYPES_SLICE,
    )
      .into()];
    v.insert(0, (*PARAMETERS)[1]);
    v.insert(0, (*PARAMETERS)[0]);
    v
  };
}

struct BallShape {
  bs: BaseShape,
  position: ParamVar,
  rotation: ParamVar,
  radius: ParamVar,
}

impl Default for BallShape {
  fn default() -> Self {
    BallShape {
      bs: BaseShape {
        shape: None,
        position: None,
      },
      position: ParamVar::new((0.0, 0.0, 0.0).into()),
      rotation: ParamVar::new((0.0, 0.0, 0.0, 1.0).into()),
      radius: ParamVar::new(0.5.into()),
    }
  }
}

impl BallShape {
  fn _cleanup(&mut self) {
    self.radius.cleanup();
  }

  fn _warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.radius.warmup(context);
    Ok(())
  }

  fn make(&mut self) -> SharedShape {
    let radius = self.radius.get().try_into().unwrap();
    SharedShape::ball(radius)
  }

  fn _parameters(&mut self) -> Option<&Parameters> {
    Some(&BALL_PARAMETERS)
  }

  fn _set_param(&mut self, index: i32, value: &Var) {
    match index {
      2 => self.radius.set_param(value),
      _ => unreachable!(),
    }
  }

  fn _get_param(&mut self, index: i32) -> Var {
    match index {
      2 => self.radius.get_param(),
      _ => unreachable!(),
    }
  }
}

shape!(BallShape, "Physics.Ball", "Physics.Ball-rust-0x20200101");

lazy_static! {
  static ref CUBE_PARAMETERS: Parameters = {
    let mut v = vec![(
      cstr!("HalfExtents"),
      shccstr!("The half-extents of the cuboid shape."),
      FLOAT3_TYPES_SLICE,
    )
      .into()];
    v.insert(0, (*PARAMETERS)[1]);
    v.insert(0, (*PARAMETERS)[0]);
    v
  };
}

struct CubeShape {
  bs: BaseShape,
  position: ParamVar,
  rotation: ParamVar,
  half_extents: ParamVar,
}

impl Default for CubeShape {
  fn default() -> Self {
    CubeShape {
      bs: BaseShape {
        shape: None,
        position: None,
      },
      position: ParamVar::new((0.0, 0.0, 0.0).into()),
      rotation: ParamVar::new((0.0, 0.0, 0.0, 1.0).into()),
      half_extents: ParamVar::new((0.5, 0.5, 0.5).into()),
    }
  }
}

impl CubeShape {
  fn _cleanup(&mut self) {
    self.half_extents.cleanup();
  }

  fn _warmup(&mut self, context: &Context) -> Result<(), &str> {
    self.half_extents.warmup(context);
    Ok(())
  }

  fn make(&mut self) -> SharedShape {
    let (x, y, z) = self.half_extents.get().try_into().unwrap();
    SharedShape::cuboid(x, y, z)
  }

  fn _parameters(&mut self) -> Option<&Parameters> {
    Some(&CUBE_PARAMETERS)
  }

  fn _set_param(&mut self, index: i32, value: &Var) {
    match index {
      2 => self.half_extents.set_param(value),
      _ => unreachable!(),
    }
  }

  fn _get_param(&mut self, index: i32) -> Var {
    match index {
      2 => self.half_extents.get_param(),
      _ => unreachable!(),
    }
  }
}

shape!(
  CubeShape,
  "Physics.Cuboid",
  "Physics.Cuboid-rust-0x20200101"
);

pub fn registerShards() {
  registerShard::<BallShape>();
  registerShard::<CubeShape>();
}
