/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::core::registerShard;
use crate::shard::Shard;
use crate::shardsc;
use crate::types::common_type;
use crate::types::ClonedVar;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::ShardsVar;
use crate::types::Type;
use crate::types::Var;
use crate::types::FRAG_CC;
use egui::Context as EguiNativeContext;
use egui::TextBuffer;
use std::ffi::c_void;

static BOOL_OR_NONE_SLICE: &[Type] = &[common_type::bool, common_type::none];

static EGUI_UI_TYPE: Type = Type::object(FRAG_CC, 1701279061); // 'eguU'
static EGUI_UI_SLICE: &'static [Type] = &[EGUI_UI_TYPE];
static EGUI_UI_SEQ_TYPE: Type = Type::seq(EGUI_UI_SLICE);

static EGUI_CTX_TYPE: Type = Type::object(FRAG_CC, 1701279043); // 'eguC'
static EGUI_CTX_SLICE: &'static [Type] = &[EGUI_CTX_TYPE];
static EGUI_CTX_VAR: Type = Type::context_variable(EGUI_CTX_SLICE);
static EGUI_CTX_VAR_TYPES: &'static [Type] = &[EGUI_CTX_VAR];

lazy_static! {
  static ref GFX_GLOBALS_TYPE: Type = unsafe { *shardsc::gfx_getMainWindowGlobalsType() };
  static ref GFX_QUEUE_TYPE: Type = unsafe { *shardsc::gfx_getQueueType() };
  static ref GFX_QUEUE_TYPES: Vec<Type> = vec![*GFX_QUEUE_TYPE];
  static ref GFX_QUEUE_VAR: Type = Type::context_variable(&GFX_QUEUE_TYPES);
  static ref GFX_QUEUE_VAR_TYPES: Vec<Type> = vec![*GFX_QUEUE_VAR];
}

const CONTEXT_NAME: &'static str = "UI.Context";
const PARENTS_UI_NAME: &'static str = "UI.Parents";

#[derive(Hash)]
struct EguiId {
  p: usize,
  idx: u8,
}

impl EguiId {
  fn new(shard: &dyn Shard, idx: u8) -> EguiId {
    EguiId {
      p: shard as *const dyn Shard as *mut c_void as usize,
      idx,
    }
  }
}

struct EguiContext {
  context: Option<EguiNativeContext>,
  instance: ParamVar,
  requiring: ExposedTypes,
  queue: ParamVar,
  contents: ShardsVar,
  main_window_globals: ParamVar,
  parents: ParamVar,
  renderer: egui_gfx::Renderer,
  input_translator: egui_gfx::InputTranslator,
}

mod containers;
mod context;
mod widgets;

mod util;

pub fn registerShards() {
  containers::registerShards();
  registerShard::<EguiContext>();
  widgets::registerShards();
}

impl AsRef<str> for ClonedVar {
  fn as_ref(&self) -> &str {
    <&str>::try_from(&self.0).unwrap()
  }
}

impl TextBuffer for ClonedVar {
  fn is_mutable(&self) -> bool {
    true
  }

  fn insert_text(&mut self, text: &str, char_index: usize) -> usize {
    let byte_idx = if !self.0.is_string() {
      0usize
    } else {
      self.byte_index_from_char_index(char_index)
    };

    // TODO to finish
    // let text_len = text.len();
    // let current_len =
    //   unsafe { self.0.payload.__bindgen_anon_1.__bindgen_anon_2.stringLen as usize };
    // let current_cap = unsafe {
    //   self
    //     .0
    //     .payload
    //     .__bindgen_anon_1
    //     .__bindgen_anon_2
    //     .stringCapacity as usize
    // };

    // if current_cap == 0usize {
    //   // this should be by the spec of String insert_str (egui uses)
    //   debug_assert!(byte_idx == 0usize);

    //   // Totally new string
    //   let s = Var::ephemeral_string(text);
    //   *self = s.into();
    // } else if (current_len - byte_idx) >= text_len {
    //   // We can fit the text in the current string
    //   // TODO
    // } else {
    //   // We need to make the string bigger
    //   let mut s = String::from(self.as_ref());
    //   s.insert_str(byte_idx, text);
    //   let s = Var::ephemeral_string(s.as_str());
    //   *self = s.into();
    // }

    let mut s = String::from(self.as_ref());
    s.insert_str(byte_idx, text);
    let s = Var::ephemeral_string(s.as_str());
    *self = s.into();

    text.chars().count()
  }

  fn delete_char_range(&mut self, char_range: std::ops::Range<usize>) {
    todo!()
  }
}
