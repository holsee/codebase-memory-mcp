defmodule Showcase.Accounts do
  @moduledoc "Context module: exercises nested-directive aliases and cross-file calls."

  # Module-body directives (D5): plain alias, multi-alias, and import — all
  # nested inside the defmodule, which the pre-Phase-0 root-only scan missed.
  alias Showcase.Accounts.User
  alias Showcase.Accounts.{Team, Membership}
  alias Showcase.Repo, as: DB
  import Showcase.Math, only: [double: 1]

  # Cross-module + cross-file call (User is in accounts/user.ex). The in-body
  # calls must attribute to register/2, not the module (D2).
  def register(id, name) when is_binary(name) do
    user = User.new(id, name)
    User.promote(user)
  end

  # Pipe chain (Phase 1c) — each stage is a remote call on an aliased module.
  def onboard(id, name) do
    id
    |> double()
    |> User.new(name)
    |> DB.insert()
  end

  def team_size(%Team{} = team), do: Membership.count(team)
end
