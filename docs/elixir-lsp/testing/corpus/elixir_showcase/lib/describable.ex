defprotocol Showcase.Describable do
  @moduledoc "A protocol with two implementations — exercises defprotocol/defimpl (D4)."
  @doc "Return a human string for the value."
  def describe(value)
end

defimpl Showcase.Describable, for: Showcase.Accounts.User do
  def describe(user), do: "user:" <> user.name
end

defimpl Showcase.Describable, for: BitString do
  def describe(s), do: "string:" <> s
end
