/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::core::cloneVar;
use crate::core::registerShard;
use crate::shard::Shard;
use crate::shardsc;
use crate::types::common_type;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::ShardsVar;
use crate::types::Type;
use crate::types::Var;
use crate::types::FRAG_CC;
use egui::Context as EguiNativeContext;
use std::ffi::c_void;
use std::ffi::CStr;

static ANY_VAR_SLICE: &[Type] = &[common_type::any, common_type::any_var];
static BOOL_OR_NONE_SLICE: &[Type] = &[common_type::bool, common_type::none];
static BOOL_VAR_OR_NONE_SLICE: &[Type] =
  &[common_type::bool, common_type::bool_var, common_type::none];
static STRING_VAR_SLICE: &[Type] = &[common_type::string, common_type::string_var];

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

struct Reset {
  parents: ParamVar,
  requiring: ExposedTypes,
}

mod containers;
mod context;
mod layouts;
mod menus;
mod reset;
mod widgets;

mod util;

enum VarTextBuffer<'a> {
  Editable(&'a mut Var),
  ReadOnly(&'a Var),
}

impl AsRef<str> for VarTextBuffer<'_> {
  fn as_ref(&self) -> &str {
    let var = match self {
      VarTextBuffer::Editable(var) => var.as_ref(),
      VarTextBuffer::ReadOnly(var) => var,
    };

    VarTextBuffer::as_str(var)
  }
}

impl egui::TextBuffer for VarTextBuffer<'_> {
  fn is_mutable(&self) -> bool {
    matches!(*self, VarTextBuffer::Editable(_))
  }

  fn insert_text(&mut self, text: &str, char_index: usize) -> usize {
    let byte_idx = if let VarTextBuffer::Editable(var) = self {
      if !var.is_string() {
        0usize
      } else {
        self.byte_index_from_char_index(char_index)
      }
    } else {
      0usize
    };

    if let VarTextBuffer::Editable(var) = self {
      let text_len = text.len();
      let (current_len, current_cap) = unsafe {
        (
          var.payload.__bindgen_anon_1.__bindgen_anon_2.stringLen as usize,
          var.payload.__bindgen_anon_1.__bindgen_anon_2.stringCapacity as usize,
        )
      };

      if current_cap == 0usize {
        // new string
        let tmp = Var::ephemeral_string(text);
        cloneVar(var, &tmp);
      } else if (current_cap - current_len) >= text_len {
        // text can fit within existing capacity
        unsafe {
          let base_ptr =
            var.payload.__bindgen_anon_1.__bindgen_anon_2.stringValue as *mut std::os::raw::c_char;
          // move the rest of the string to the end
          std::ptr::copy(
            base_ptr.add(byte_idx),
            base_ptr.add(byte_idx).add(text_len),
            current_len - byte_idx,
          );
          // insert the new text
          let bytes = text.as_ptr() as *const std::os::raw::c_char;
          std::ptr::copy_nonoverlapping(bytes, base_ptr.add(byte_idx), text_len);
          // update the length
          let new_len = current_len + text_len;
          var.payload.__bindgen_anon_1.__bindgen_anon_2.stringLen = new_len as u32;
          // fixup null-terminator
          *base_ptr.add(new_len) = 0;
        }
      } else {
        let mut str = String::from(VarTextBuffer::as_str(var));
        str.insert_str(byte_idx, text);
        let tmp = Var::ephemeral_string(str.as_str());
        cloneVar(var, &tmp);
      }

      text.chars().count()
    } else {
      0usize
    }
  }

  fn delete_char_range(&mut self, char_range: std::ops::Range<usize>) {
    assert!(char_range.start <= char_range.end);

    let byte_start = self.byte_index_from_char_index(char_range.start);
    let byte_end = self.byte_index_from_char_index(char_range.end);

    if byte_start == byte_end {
      // nothing to do
      return;
    }

    if let VarTextBuffer::Editable(var) = self {
      unsafe {
        let current_len = var.payload.__bindgen_anon_1.__bindgen_anon_2.stringLen as usize;
        let base_ptr =
          var.payload.__bindgen_anon_1.__bindgen_anon_2.stringValue as *mut std::os::raw::c_char;
        // move rest of the text at the deletion location
        std::ptr::copy(
          base_ptr.add(byte_end),
          base_ptr.add(byte_start),
          current_len - byte_end,
        );
        // update the length
        let new_len = current_len - byte_end + byte_start;
        var.payload.__bindgen_anon_1.__bindgen_anon_2.stringLen = new_len as u32;
        // fixup null-terminator
        *base_ptr.add(new_len) = 0;
      }
    }
  }
}

impl VarTextBuffer<'_> {
  fn as_str(var: &Var) -> &str {
    if var.valueType != shardsc::SHType_String
      && var.valueType != shardsc::SHType_Path
      && var.valueType != shardsc::SHType_ContextVar
      && var.valueType != shardsc::SHType_None
    {
      panic!("Expected None, String, Path or ContextVar variable, but casting failed.")
    }

    if var.valueType == shardsc::SHType_None {
      return "";
    }

    unsafe {
      CStr::from_ptr(
        var.payload.__bindgen_anon_1.__bindgen_anon_2.stringValue as *mut std::os::raw::c_char,
      )
      .to_str()
      .unwrap()
    }
  }
}

pub fn registerShards() {
  containers::registerShards();
  registerShard::<EguiContext>();
  layouts::registerShards();
  menus::registerShards();
  registerShard::<Reset>();
  widgets::registerShards();
}
