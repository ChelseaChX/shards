/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::core::registerShard;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::ShardsVar;

struct Area {
  instance: ParamVar,
  requiring: ExposedTypes,
  position: ParamVar,
  anchor: ParamVar,
  contents: ShardsVar,
  parents: ParamVar,
  exposing: ExposedTypes,
}

struct Scope {
  parents: ParamVar,
  requiring: ExposedTypes,
  contents: ShardsVar,
  exposing: ExposedTypes,
}

/// Standalone window.
struct Window {
  instance: ParamVar,
  requiring: ExposedTypes,
  title: ParamVar,
  position: ParamVar,
  width: ParamVar,
  height: ParamVar,
  contents: ShardsVar,
  parents: ParamVar,
  exposing: ExposedTypes,
}

macro_rules! decl_panel {
  ($name:ident) => {
    struct $name {
      instance: ParamVar,
      requiring: ExposedTypes,
      contents: ShardsVar,
      parents: ParamVar,
      exposing: ExposedTypes,
    }
  };
}

decl_panel!(BottomPanel);
decl_panel!(CentralPanel);
decl_panel!(LeftPanel);
decl_panel!(RightPanel);
decl_panel!(TopPanel);

mod area;
mod panels;
mod scope;
mod window;

pub fn registerShards() {
  registerShard::<Area>();
  registerShard::<Scope>();
  registerShard::<Window>();
  registerShard::<BottomPanel>();
  registerShard::<CentralPanel>();
  registerShard::<LeftPanel>();
  registerShard::<RightPanel>();
  registerShard::<TopPanel>();
}
