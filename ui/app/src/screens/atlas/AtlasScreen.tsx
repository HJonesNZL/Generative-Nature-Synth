/** Screen 1 — Atlas (approved design: H2 · Region Mix). */
import { useCatalog } from "../../app/catalog";
import { useAppState } from "../../app/store";
import { CoverageLegend, GrainPanel } from "../shared/GrainPanel";
import { useMaterialView } from "../shared/useMaterialView";
import { LeftColumn } from "./LeftColumn";
import { MapStage } from "./MapStage";
import { RightColumn } from "./RightColumn";

export function AtlasScreen() {
  const view = useMaterialView();
  const catalog = useCatalog();
  const { mode } = useAppState();
  return (
    <main className="screen atlas" id="main" aria-label="Atlas">
      <LeftColumn view={view} catalog={catalog} />
      <MapStage view={view} catalog={catalog} />
      <RightColumn view={view} catalog={catalog} />
      <GrainPanel view={view} catalog={catalog} aside={mode === "history" ? <CoverageLegend /> : null} />
    </main>
  );
}
