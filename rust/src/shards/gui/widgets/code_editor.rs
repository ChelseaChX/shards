/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use super::CodeEditor;
use crate::shard::Shard;
use crate::shards::gui::util;
use crate::shards::gui::VarTextBuffer;
use crate::shards::gui::EGUI_UI_SEQ_TYPE;
use crate::shards::gui::PARENTS_UI_NAME;
use crate::shards::gui::STRING_VAR_SLICE;
use crate::shardsc;
use crate::types::common_type;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::InstanceData;
use crate::types::OptionalString;
use crate::types::ParamVar;
use crate::types::Parameters;
use crate::types::Type;
use crate::types::Types;
use crate::types::Var;
use crate::types::BOOL_TYPES_SLICE;
use crate::types::NONE_TYPES;
use crate::types::STRING_TYPES;
use egui::epaint::text::layout;
use egui::text::LayoutJob;
use std::cmp::Ordering;
use std::ffi::CStr;
use syntect::highlighting::Theme;
use syntect::highlighting::ThemeSet;
use syntect::parsing::SyntaxSet;

lazy_static! {
  static ref CODEEDITOR_PARAMETERS: Parameters = vec![(
    cstr!("Variable"),
    cstr!("The variable that holds the input value."),
    STRING_VAR_SLICE,
  )
    .into(),];
}

impl Default for CodeEditor {
  fn default() -> Self {
    let mut parents = ParamVar::default();
    parents.set_name(PARENTS_UI_NAME);
    Self {
      parents,
      requiring: Vec::new(),
      variable: ParamVar::default(),
      exposing: Vec::new(),
      should_expose: false,
      mutable_text: true,
    }
  }
}

impl Shard for CodeEditor {
  fn registerName() -> &'static str
  where
    Self: Sized,
  {
    cstr!("UI.CodeEditor")
  }

  fn hash() -> u32
  where
    Self: Sized,
  {
    compile_time_crc32::crc32!("UI.CodeEditor-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "UI.CodeEditor"
  }

  fn help(&mut self) -> OptionalString {
    OptionalString(shccstr!("TODO"))
  }

  fn inputTypes(&mut self) -> &Types {
    &NONE_TYPES
  }

  fn inputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!("TODO"))
  }

  fn outputTypes(&mut self) -> &Types {
    &STRING_TYPES
  }

  fn outputHelp(&mut self) -> OptionalString {
    OptionalString(shccstr!("TODO"))
  }

  fn parameters(&mut self) -> Option<&Parameters> {
    Some(&CODEEDITOR_PARAMETERS)
  }

  fn setParam(&mut self, index: i32, value: &Var) -> Result<(), &str> {
    match index {
      0 => Ok(self.variable.set_param(value)),
      _ => Err("Invalid parameter index"),
    }
  }

  fn getParam(&mut self, index: i32) -> Var {
    match index {
      0 => self.variable.get_param(),
      _ => Var::default(),
    }
  }

  fn hasCompose() -> bool {
    true
  }

  fn compose(&mut self, data: &InstanceData) -> Result<Type, &str> {
    if self.variable.is_variable() {
      self.should_expose = true; // assume we expose a new variable

      let shared: ExposedTypes = data.shared.into();
      for var in shared {
        let (a, b) = unsafe {
          (
            CStr::from_ptr(var.name),
            CStr::from_ptr(self.variable.get_name()),
          )
        };
        if CStr::cmp(&a, &b) == Ordering::Equal {
          self.should_expose = false;
          self.mutable_text = var.isMutable;
          if var.exposedType.basicType != shardsc::SHType_String {
            return Err("TextInput: string variable required.");
          }
          break;
        }
      }
    } else {
      self.mutable_text = false;
    }

    Ok(common_type::string)
  }

  fn exposedVariables(&mut self) -> Option<&ExposedTypes> {
    if self.variable.is_variable() && self.should_expose {
      self.exposing.clear();

      let exp_info = ExposedInfo {
        exposedType: common_type::string,
        name: self.variable.get_name(),
        help: cstr!("The exposed string variable").into(),
        ..ExposedInfo::default()
      };

      self.exposing.push(exp_info);
      Some(&self.exposing)
    } else {
      None
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
    self.variable.warmup(ctx);

    if self.should_expose {
      self.variable.get_mut().valueType = common_type::string.basicType;
    }

    Ok(())
  }

  fn cleanup(&mut self) -> Result<(), &str> {
    self.variable.cleanup();
    self.parents.cleanup();

    Ok(())
  }

  fn activate(&mut self, _context: &Context, _input: &Var) -> Result<Var, &str> {
    if let Some(ui) = util::get_current_parent(*self.parents.get())? {
      let theme = if ui.style().visuals.dark_mode {
        CodeTheme::dark()
      } else {
        CodeTheme::light()
      };
      let mut layouter = |ui: &egui::Ui, string: &str, wrap_width: f32| {
        let mut layout_job = highlight(ui.ctx(), &theme, string, "Clojure");
        layout_job.wrap.max_width = wrap_width;
        ui.fonts().layout_job(layout_job)
      };
      let text = &mut if self.mutable_text {
        VarTextBuffer::Editable(self.variable.get_mut())
      } else {
        VarTextBuffer::ReadOnly(self.variable.get())
      };
      let code_editor = egui::TextEdit::multiline(text)
        .code_editor()
        .desired_rows(10)
        .desired_width(f32::INFINITY)
        .layouter(&mut layouter);
      let response = egui::ScrollArea::vertical()
        .show(ui, |ui| ui.add(code_editor))
        .inner;

      if response.changed() || response.lost_focus() {
        Ok(*self.variable.get())
      } else {
        Ok(Var::default())
      }
    } else {
      Err("No UI parent")
    }
  }
}

/// Memoized Code highlighting
fn highlight(ctx: &egui::Context, theme: &CodeTheme, code: &str, language: &str) -> LayoutJob {
  impl egui::util::cache::ComputerMut<(&CodeTheme, &str, &str), LayoutJob> for Highlighter {
    fn compute(&mut self, (theme, code, language): (&CodeTheme, &str, &str)) -> LayoutJob {
      self.highlight(theme, code, language)
    }
  }

  type HighlightCache<'a> = egui::util::cache::FrameCache<LayoutJob, Highlighter>;

  let mut memory = ctx.memory();
  let highlight_cache = memory.caches.cache::<HighlightCache<'_>>();
  highlight_cache.get((theme, code, language))
}

#[derive(Hash)]
struct CodeTheme {
  dark_mode: bool,
  syntect_theme: SyntectTheme,
}

impl Default for CodeTheme {
  fn default() -> Self {
    Self::dark()
  }
}

impl CodeTheme {
  pub fn dark() -> Self {
    Self {
        dark_mode: true,
        syntect_theme: SyntectTheme::Base16MochaDark,
    }
  }

  pub fn light() -> Self {
    Self {
      dark_mode: false,
      syntect_theme: SyntectTheme::SolarizedLight,
    }
  }
}

struct Highlighter {
  syntaxes: SyntaxSet,
  themes: ThemeSet,
}

impl Default for Highlighter {
  fn default() -> Self {
    // FIXME: build from definition of our language
    let ss = SyntaxSet::load_defaults_newlines();
    for s in ss.syntaxes() {      
      shlog!("Lang: {}, Extensions: {}", s.name, s.file_extensions.join(","));
    }
    Self {
      syntaxes: SyntaxSet::load_defaults_newlines(),
      themes: ThemeSet::load_defaults(),
    }
  }
}

impl Highlighter {
  fn highlight(&self, theme: &CodeTheme, text: &str, language: &str) -> LayoutJob {
    self
      .highlight_impl(theme, text, language)
      .unwrap_or_else(|| {
        LayoutJob::simple(
          text.into(),
          egui::FontId::monospace(14.0),
          if theme.dark_mode {
            egui::Color32::LIGHT_GRAY
          } else {
            egui::Color32::DARK_GRAY
          },
          f32::INFINITY,
        )
      })
  }

  fn highlight_impl(&self, theme: &CodeTheme, text: &str, language: &str) -> Option<LayoutJob> {
    use syntect::easy::HighlightLines;
    use syntect::highlighting::FontStyle;
    use syntect::util::LinesWithEndings;

    let syntax = self
      .syntaxes
      .find_syntax_by_name(language)
      .or_else(|| self.syntaxes.find_syntax_by_extension(language))?;

    let theme = theme.syntect_theme.syntect_key_name();
    let mut h = HighlightLines::new(syntax, &self.themes.themes[theme]);

    use egui::text::{LayoutSection, TextFormat};

    let mut job = LayoutJob {
      text: text.into(),
      ..Default::default()
    };

    for line in LinesWithEndings::from(text) {
      for (style, range) in h.highlight_line(line, &self.syntaxes).ok()? {
        let fg = style.foreground;
        let text_color = egui::Color32::from_rgb(fg.r, fg.g, fg.b);
        let italics = style.font_style.contains(FontStyle::ITALIC);
        let underline = style.font_style.contains(FontStyle::ITALIC);
        let underline = if underline {
          egui::Stroke::new(1.0, text_color)
        } else {
          egui::Stroke::none()
        };
        job.sections.push(LayoutSection {
          leading_space: 0.0,
          byte_range: as_byte_range(text, range),
          format: TextFormat {
            font_id: egui::FontId::monospace(14.0),
            color: text_color,
            italics,
            underline,
            ..Default::default()
          },
        });
      }
    }

    Some(job)
  }
}

#[derive(Hash)]
enum SyntectTheme {
  Base16EightiesDark,
  Base16MochaDark,
  Base16OceanDark,
  Base16OceanLight,
  InspiredGitHub,
  SolarizedDark,
  SolarizedLight,
}

impl SyntectTheme {
  fn syntect_key_name(&self) -> &'static str {
    match self {
      Self::Base16EightiesDark => "base16-eighties.dark",
      Self::Base16MochaDark => "base16-mocha.dark",
      Self::Base16OceanDark => "base16-ocean.dark",
      Self::Base16OceanLight => "base16-ocean.light",
      Self::InspiredGitHub => "InspiredGitHub",
      Self::SolarizedDark => "Solarized (dark)",
      Self::SolarizedLight => "Solarized (light)",
    }
  }
}

fn as_byte_range(whole: &str, range: &str) -> std::ops::Range<usize> {
  let whole_start = whole.as_ptr() as usize;
  let range_start = range.as_ptr() as usize;
  assert!(whole_start <= range_start);
  assert!(range_start + range.len() <= whole_start + whole.len());
  let offset = range_start - whole_start;
  offset..(offset + range.len())
}
