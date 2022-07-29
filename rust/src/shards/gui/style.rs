/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use egui::Color32;

use super::Style;
use crate::shard::Shard;
use crate::shards::gui::util;
use crate::shards::gui::EGUI_UI_SEQ_TYPE;
use crate::shards::gui::PARENTS_UI_NAME;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::Types;
use crate::types::Var;
use crate::types::ANY_TYPES;
use crate::types::STRING_OR_NONE_SLICE;

lazy_static! {
  static ref STYLE_PARAMETERS: Parameters = vec![
    (cstr!("Color"), cstr!(""), STRING_OR_NONE_SLICE,).into(),
  ];
}
impl Default for Style {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      color: ParamVar::default(),
    }
  }
}

impl Shard for Style {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.Style")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.Style-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.Style"
  }

  fn inputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&STYLE_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => Ok(self.color.set_param(value)),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.color.get_param(),
      _ => Var::default(),
    }
  }

  fn requiredVariables(&mut self) -> Option<&ExposedTypes> {
    self.requiring.clear();

    // Add UI.Parents to the list of required variables
    let exp_info = ExposedInfo {
      exposedType: EGUI_UI_SEQ_TYPE,
      name: self.parents.get_name(),
      help: cstr!("The parent UI objects.").into(),
      ..ExposedInfo::default()
    };
    self.requiring.push(exp_info);

    Some(&self.requiring)
  }

  fn warmup(&mut self, ctx: &Context) -> Result<(), &str> {
    self.parents.warmup(ctx);
    self.color.warmup(ctx);

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.color.cleanup();
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, _context: &Context, input: &Var) -> Result<Var, &str> {
    if let Some(ui) = util::get_current_parent(*self.parents.get())? {

      // let default_color = ui.visuals().text_color();
      let color = match self.color.get().try_into()? {
        "Blue" => Some(Color32::BLUE),
        "White" => Some(Color32::WHITE),
        "Red" => Some(Color32::RED),
        _ => None,
      };
      ui.visuals_mut().override_text_color = color;
      
      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }
}
