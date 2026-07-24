defmodule Showcase.Accounts.User do
  @moduledoc "A user with guarded constructors and multi-clause helpers."

  defstruct [:id, :name, :role]

  @doc "Adults are 18+. defguard exercises the defguard def-like form."
  defguard is_adult(age) when is_integer(age) and age >= 18

  # Guarded def head (D1) — parses as binary_operator(left, "when", guard).
  def new(id, name) when is_integer(id) and is_binary(name) do
    %Showcase.Accounts.User{id: id, name: name, role: :member}
  end

  # Default argument (D3 / Phase 1c) — fans out to promote/1 and promote/2.
  def promote(user, role \\ :admin) do
    %{user | role: role}
  end

  # Multi-clause function — three clauses, one logical function/1 (D3).
  def label(%Showcase.Accounts.User{role: :admin}), do: "admin"
  def label(%Showcase.Accounts.User{role: :member}), do: "member"
  def label(%Showcase.Accounts.User{}), do: "unknown"

  defp secret, do: :hidden
end
