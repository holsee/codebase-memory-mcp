defmodule Showcase.Math do
  @moduledoc "Macros, pipes, and captures (D4 / Phase 1c)."

  @doc "A macro def-like form (D4)."
  defmacro const(value) do
    quote do: unquote(value)
  end

  def double(x), do: x * 2

  # Capture syntax &Mod.fun/1 and a pipe chain (Phase 1c).
  def scale_all(list) do
    list
    |> Enum.map(&double/1)
    |> Enum.sum()
  end

  # __MODULE__ reference plus a same-module call.
  def describe do
    "math:" <> Atom.to_string(__MODULE__)
  end
end
