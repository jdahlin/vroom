# struct pp in sub-structs (proposal, not applied)

`struct pp` has about 150 fields.  The names below are the new field names
of `rename-lexpp.tsv`; a group becomes a member (`p->output.length`), so
`struct pp` ends with about 15 members.  Applying it is step 5 of
`WORK-lexpp.md`, by script, in the same change as the field renames.  Fields
the diagnostics files read (`diagnostic_*`, `macro_definition_site*`,
`files`, `line_marks`) move at the same time: `src/frontend/diag/` is edited by
the script, not by hand.

| Member | Fields |
|---|---|
| `options`, `fatal_jump`, `base_file_name`, `base_file_name_length`, `builtin_file` | stay at the top: what the TU is |
| `macros` | `macro_directory`, `pushed_macros`, `pushed_macro_count`, `counter_value`, `warning_cache`, `warning_cache_options` |
| `stack` (the context stack) | `contexts`, `context_depth`, `file_context_count`, `scratch_tokens`, `scratch_token_count`, `owed_spacing`, `argument_preexpansion`, `in_directive`, `collecting_arguments`, `collecting_location`, `collecting_name`, `token_unrecorded` |
| `memory` | `permanent_region`, `temporary_region`, `temporary_held` |
| `files` | `files`, `file_count`, `next_location_base`, `once_files`, `once_file_count`, `once_file_capacity`, `line_marks`, `line_mark_count` |
| `delivery` (what the front end sees) | `tokens_delivered`, `tokens_skipped`, `top_events`, `top_event_count`, `top_events_full`, `top_region_recording`, `hit_regions`, `hit_region_count`, `open_hits`, `open_hit_count` |
| `diagnostics` | `error_count`, `warning_count`, `diagnostic_color`, `diagnostic_last_*` (3), `diagnostic_head`, `diagnostic_tail`, `diagnostic_track`, `diagnostic_finished`, `diagnostic_pending_structured`, `diagnostic_pending_text`, `diagnostic_previous_*` (2) |
| `diagnostic_state` | `diagnostic_nodes`, `diagnostic_node_count`, `diagnostic_transitions`, `diagnostic_transition_count`, `diagnostic_current`, `diagnostic_stack`, `diagnostic_stack_depth`, `diagnostic_enabling_count` |
| `macro_sites` | `macro_definition_sites`, `..._count`, `..._capacity`, `..._files` |
| `output` (the `-E` printer) | `output`, `output_length`, `output_capacity`, `output_column`, `output_line`, `output_mode`, `output_text_mode`, `previous_kind`, `previous_is_wide_prefix`, `previous_after_period`, `previous_end`, `line_start_name_active`, `line_start_context_depth`, `line_start_location`, `expansion_location`, `directive_on_line`, `comment_starts_line`, `embed_space_pending`, `embed_tail` |
| `markers` | `markers_on`, `marker_builtin_printed`, `marker_is_system`, `marker_predefine_count`, `marker_builtin_line`, `marker_file_name`, `marker_file_name_length` |
| `define_events` | `define_events`, `define_event_count`, `define_event_capacity`, `define_events_printed` |
| `header_cache` | `header_cache_on`, `header_cache_last_entry`, `options_signature`, `search_path_signatures`, `recording` (already a struct), `recording_stamps`, `child_verdicts`, `child_verdict_generation`, `entry_bytes`, `blob_cache`, `punctuator_blobs`, `chunk_cache_by_hash`, `chunk_cache_by_ids` |

Hot fields (read by `pp_token_next` and `pp_token_raw_next`: `contexts`,
`context_depth`, `file_context_count`, `owed_spacing`, `scratch_token_count`,
`temporary_held`, `tokens_delivered`, `output_mode`, `recording.on`) stay
within the first cache lines of `struct pp`: the order of the members is
chosen with `vbench.py gate`, not by this list.
