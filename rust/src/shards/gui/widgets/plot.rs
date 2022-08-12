/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::shard::Shard;
use crate::shards::gui::util;
use crate::shards::gui::EguiId;
use crate::shards::gui::PARENTS_UI_NAME;
use crate::shardsc;
use crate::types::common_type;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::InstanceData;
use crate::types::OptionalString;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::Seq;
use crate::types::ShardsVar;
use crate::types::Type;
use crate::types::Types;
use crate::types::Var;
use crate::types::ANY_TYPES;
use crate::types::FLOAT2_TYPES;
use crate::types::FRAG_CC;
use crate::types::SHARDS_OR_NONE_TYPES;

use super::Plot;
use super::PlotLine;

const LINES_NAME: &'static str = "UI.Lines";

lazy_static! {
  pub static ref SEQ_OF_FLOAT2: Type = Type::seq(&FLOAT2_TYPES);
  pub static ref SEQ_OF_FLOAT2_TYPES: Vec<Type> = vec![*SEQ_OF_FLOAT2];
  static ref PLOT_PARAMETERS: Parameters = vec![(
    cstr!("Contents"),
    cstr!("The UI contents."),
    &SHARDS_OR_NONE_TYPES[..],
  )
    .into(),];
}

impl Default for Plot {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    let mut lines = ParamVar::default();
    lines.set_name(LINES_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      contents: ShardsVar::default(),
      exposing: Vec::new(),
      lines,
    }
  }
}

impl Default for PlotLine {
  fn default() -> Self {
    let mut lines = ParamVar::default();
    lines.set_name(LINES_NAME);
    Self { lines }
  }
}

impl Shard for Plot {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.Plot")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.Plot-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.Plot"
  }

  fn inputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &ANY_TYPES
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&PLOT_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => self.contents.set_param(value),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.contents.get_param(),
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

    Ok(data.inputType)
  }

  fn warmup(&mut self, ctx: &Context) -> Result<(), &str> {
    self.parents.warmup(ctx);
    self.lines.warmup(ctx);
    self.lines.set(Seq::new().as_ref().into());

    if !self.contents.is_empty() {
      self.contents.warmup(ctx)?;
    }

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    if !self.contents.is_empty() {
      self.contents.cleanup();
    }

    self.lines.cleanup();
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, context: &Context, input: &Var) -> Result<Var, &str> {
    if self.contents.is_empty() {
      return Ok(*input);
    }

    if let Some(ui) = util::get_current_parent(*self.parents.get())? {
      // pass the lines to the inner shards
      let var = self.lines.get();
      let mut seq: Seq = var.try_into()?;
      seq.clear();

      self.lines.set(seq.as_ref().into());

      let mut _output = Var::default();
      self.contents.activate(context, input, &mut _output);

      egui::plot::Plot::new(EguiId::new(self, 0)).show(ui, |plot_ui| {
        // get the lines from the inner shards
        let var = self.lines.get();
        let seq: Seq = var.try_into()?;
        for s in seq {
          let values: &[Var] = s.try_into()?;
          let values = values.iter().map(|x| {
            let v: (f64, f64) = x.try_into().unwrap();
            egui::plot::Value::new(v.0, v.1)
          });
          let line = egui::plot::Line::new(egui::plot::Values::from_values_iter(values));
          plot_ui.line(line);
        }

        Ok::<(), &str>(())
      });

      Ok(*input)
    } else {
      Err("No UI parent")
    }
  }
}

impl Shard for PlotLine {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.PlotLine")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.PlotLine-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.PlotLine"
  }

  fn inputTypes(&mut self) -> &Types {
    &SEQ_OF_FLOAT2_TYPES
  }

  fn outputTypes(&mut self) -> &Types {
    &SEQ_OF_FLOAT2_TYPES
  }

  fn warmup(&mut self, ctx: &Context) -> Result<(), &str> {
    self.lines.warmup(ctx);

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.lines.cleanup();

    Ok(())
  }

  fn activate(&mut self, _context: &Context, input: &Var) -> Result<Var, &str> {
    let var = self.lines.get();
    let mut seq: Seq = var.try_into()?;
    seq.push(*input);
    self.lines.set(seq.as_ref().into());

    Ok(*input)
  }
}
