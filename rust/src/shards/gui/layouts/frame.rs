/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use super::Frame;
use crate::shard::Shard;
use crate::shards::gui::util;
use crate::shards::gui::PARENTS_UI_NAME;
use crate::shardsc::SHColor;
use crate::types::Context;
use crate::types::ExposedTypes;
use crate::types::InstanceData;
use crate::types::OptionalString;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::ShardsVar;
use crate::types::Type;
use crate::types::Types;
use crate::types::Var;
use crate::types::ANY_TYPES;
use crate::types::COLOR_TYPES_SLICE;
use crate::types::FLOAT4_TYPES;
use crate::types::FLOAT_TYPES_SLICE;
use crate::types::SHARDS_OR_NONE_TYPES;

lazy_static! {
  static ref FRAME_PARAMETERS: Parameters = vec![
    (
      cstr!("Contents"),
      cstr!("The UI contents."),
      &SHARDS_OR_NONE_TYPES[..],
    )
      .into(),
    (
      cstr!("InnerMargin"),
      cstr!("The UI contents."),
      &FLOAT4_TYPES[..],
    )
      .into(),
    (
      cstr!("OuterMargin"),
      cstr!("The UI contents."),
      &FLOAT4_TYPES[..],
    )
      .into(),
    (
      cstr!("Rounding"),
      cstr!("The UI contents."),
      &FLOAT4_TYPES[..],
    )
      .into(),
    (
      cstr!("FillColor"),
      cstr!("The UI contents."),
      COLOR_TYPES_SLICE,
    )
      .into(),
    (
      cstr!("StrokeColor"),
      cstr!("The UI contents."),
      COLOR_TYPES_SLICE,
    )
      .into(),
    (
      cstr!("StrokeWidth"),
      cstr!("The UI contents."),
      FLOAT_TYPES_SLICE,
    )
      .into(),
  ];
}

impl Default for Frame {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      contents: ShardsVar::default(),
      innerMargin: ParamVar::default(),
      outerMargin: ParamVar::default(),
      rounding: ParamVar::default(),
      fillColor: ParamVar::default(),
      strokeColor: ParamVar::default(),
      strokeWidth: ParamVar::default(),
      exposing: Vec::new(),
    }
  }
}

impl Shard for Frame {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.Frame")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.Frame-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.Frame"
  }

  fn help(&mut self) -> OptionalString {
    OptionalString(shccstr!("Visually groups the contents together."))
  }

  fn inputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn inputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!(
      "The value that will be passed to the Contents shards of the frame."
    ))
  }

  fn outputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn outputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!("The output of this shard will be its input."))
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&FRAME_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => self.contents.set_param(value),
      1 => Ok(self.innerMargin.set_param(value)),
      2 => Ok(self.outerMargin.set_param(value)),
      3 => Ok(self.rounding.set_param(value)),
      4 => Ok(self.fillColor.set_param(value)),
      5 => Ok(self.strokeColor.set_param(value)),
      6 => Ok(self.strokeWidth.set_param(value)),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.contents.get_param(),
      1 => self.innerMargin.get_param(),
      2 => self.outerMargin.get_param(),
      3 => self.rounding.get_param(),
      4 => self.fillColor.get_param(),
      5 => self.strokeColor.get_param(),
      6 => self.strokeWidth.get_param(),
      _ => Var::default(),
    }
  }

  fn requiredVariables(&mut self) -> Option<&ExposedTypes> {
    self.requiring.clear();

    // Add UI.Parents to the list of required variables
    util::require_parents(&mut self.requiring, &self.parents);

    Some(&self.requiring)
  }

  fn exposedVariables(&mut self) -> Option<&ExposedTypes> {
    self.exposing.clear();

    if util::expose_contents_variables(&mut self.exposing, &self.contents) {
      Some(&self.exposing)
    } else {
      None
    }
  }

  fn hasCompose() -> bool {
    true
  }

  fn compose(&mut self, data: &InstanceData) -> Result<Type, &str> {
    if !self.contents.is_empty() {
      self.contents.compose(&data)?;
    }

    // Always passthrough the input
    Ok(data.inputType)
  }

  fn warmup(&mut self, ctx: &Context) -> Result<(), &str> {
    self.parents.warmup(ctx);
    if !self.contents.is_empty() {
      self.contents.warmup(ctx)?;
    }
    self.innerMargin.warmup(ctx);
    self.outerMargin.warmup(ctx);
    self.rounding.warmup(ctx);
    self.fillColor.warmup(ctx);
    self.strokeColor.warmup(ctx);
    self.strokeWidth.warmup(ctx);

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.strokeWidth.cleanup();
    self.strokeColor.cleanup();
    self.fillColor.cleanup();
    self.rounding.cleanup();
    self.outerMargin.cleanup();
    self.innerMargin.cleanup();
    if !self.contents.is_empty() {
      self.contents.cleanup();
    }
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, context: &Context, input: &Var) -> Result<Var, &str> {
    if self.contents.is_empty() {
      return Ok(*input);
    }

    if let Some(ui) = util::get_current_parent(*self.parents.get())? {
      let inner_margin = self.innerMargin.get();
      let inner_margin = if inner_margin.is_none() {
        egui::style::Margin::default()
      } else {
        let (left, right, top, bottom) = inner_margin.try_into()?;
        egui::style::Margin {
          left,
          right,
          top,
          bottom,
        }
      };
      let outer_margin = self.outerMargin.get();
      let outer_margin = if outer_margin.is_none() {
        egui::style::Margin::default()
      } else {
        let (left, right, top, bottom) = outer_margin.try_into()?;
        egui::style::Margin {
          left,
          right,
          top,
          bottom,
        }
      };
      let rounding = self.rounding.get();
      let rounding = if rounding.is_none() {
        ui.style().visuals.widgets.noninteractive.rounding
      } else {
        let (nw, ne, sw, se) = rounding.try_into()?;
        egui::epaint::Rounding { nw, ne, sw, se }
      };
      let fill = self.fillColor.get();
      let fill = if fill.is_none() {
        ui.style().visuals.widgets.noninteractive.bg_fill
      } else {
        let color: SHColor = fill.try_into()?;
        egui::Color32::from_rgba_unmultiplied(color.r, color.g, color.b, color.a)
      };
      let stroke = {
        let width = self.strokeWidth.get();
        let width = if width.is_none() {
          ui.style().visuals.widgets.noninteractive.bg_stroke.width
        } else {
          width.try_into()?
        };
        let color = self.strokeColor.get();
        let color = if color.is_none() {
          ui.style().visuals.widgets.noninteractive.bg_stroke.color
        } else {
          let color: SHColor = color.try_into()?;
          egui::Color32::from_rgba_unmultiplied(color.r, color.g, color.b, color.a)
        };
        egui::epaint::Stroke { width, color }
      };
      let frame = egui::Frame {
        inner_margin,
        outer_margin,
        rounding,
        fill,
        stroke,
        ..Default::default()
      };
      frame
        .show(ui, |ui| {
          util::activate_ui_contents(context, input, ui, &mut self.parents, &mut self.contents)
        })
        .inner?;

      // Always passthrough the input
      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }
}
