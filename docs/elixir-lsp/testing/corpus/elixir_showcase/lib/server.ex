defmodule Showcase.Server do
  @moduledoc "A GenServer exercising use-macro callbacks, nesting, and guards."
  use GenServer

  require Logger

  # Nested module (D6) — must be extracted as Showcase.Server.State, not State.
  defmodule State do
    defstruct count: 0, name: nil
  end

  def start_link(name) do
    GenServer.start_link(__MODULE__, name, name: __MODULE__)
  end

  @impl true
  def init(name) do
    {:ok, %Showcase.Server.State{name: name}}
  end

  # Guarded callback clause (D1) + in-body call attribution (D2).
  @impl true
  def handle_call({:bump, by}, _from, state) when is_integer(by) and by > 0 do
    next = tick(state, by)
    {:reply, next.count, next}
  end

  @impl true
  def handle_cast(:reset, state) do
    Logger.info("reset")
    {:noreply, %{state | count: 0}}
  end

  defp tick(state, by), do: %{state | count: state.count + by}
end
