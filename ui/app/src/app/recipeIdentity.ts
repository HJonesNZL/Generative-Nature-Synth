import type { HostMethods } from "../host/contracts";
interface RecipeSnapshot { settings: unknown; preparation: HostMethods["prepareReplay"]["response"] | null }
/** A sibling recipe/retime may advance preparation, but never rebase over own edits from another window. */
export function recipeStillCurrent(fresh: RecipeSnapshot, accepted: RecipeSnapshot): boolean {
  const source = accepted.preparation?.session?.id;
  return !!source && source === fresh.preparation?.session?.id && JSON.stringify(fresh.settings) === JSON.stringify(accepted.settings);
}
