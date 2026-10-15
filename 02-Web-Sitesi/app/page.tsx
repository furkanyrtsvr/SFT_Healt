import { getChatGPTUser } from "./chatgpt-auth";
import HealthDashboard from "./health-dashboard";

export default async function Home() {
  const user = await getChatGPTUser();

  return (
    <HealthDashboard
      viewerName={user?.displayName ?? "Yerel önizleme"}
      signedIn={Boolean(user)}
    />
  );
}
