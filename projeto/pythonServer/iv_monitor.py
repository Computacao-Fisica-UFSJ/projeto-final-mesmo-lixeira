import time
from collections import deque

# https://www.youtube.com/watch?v=yoRuij-e6zc

# Monitor de Infusão Intravenosa (IV)
class IVMonitor:
    def __init__(self, total_volume_ml, dripping_factor):
        self.total_volume_ml = total_volume_ml if total_volume_ml is not None else 500
        self.dripping_factor = dripping_factor if dripping_factor is not None else 20
        
        # Estado atual
        self.total_drips = 0
        self.infusion_start = None
        self.last_drip_time = None
        
        self.drip_history = deque(maxlen=10) # Para cálculo de fluxo (média móvel das últimas 10 gotas)

    def register_drip(self):
        now = time.time()
        
        if self.total_drips == 0:
            self.infusion_start = now
        
        self.total_drips += 1
        self.last_drip_time = now
        self.drip_history.append(now)
        
    def calculate_values(self):
        if self.total_drips == 0:
            return None

        infused_volume_ml = self.total_drips / self.dripping_factor # Volume infundido
        percentage = (infused_volume_ml / self.total_volume_ml) * 100 # Porcentagem concluída
        
        # Cálculo do fluxo atual (gts/min)
        drips_per_minute = 0
        if len(self.drip_history) > 1:
            delta_t = self.drip_history[-1] - self.drip_history[0]
            interval_quantity = len(self.drip_history) - 1
            if delta_t > 0:
                seconds_per_drip = delta_t / interval_quantity
                drips_per_minute = 60 / seconds_per_drip
        
        remaining_time_minutes = 0 # Tempo restante estimado
        remaining_volume_ml = self.total_volume_ml - infused_volume_ml # Volume restante
        ml_per_minute = drips_per_minute / self.dripping_factor # ml/min

        if ml_per_minute > 0:
            remaining_time_minutes = remaining_volume_ml / ml_per_minute
        
        time_elapsed = time.time() - self.infusion_start # Tempo decorrido desde o início da infusão

        return {
            "total_drips": self.total_drips,
            "infused_volume_ml": round(infused_volume_ml, 2),
            "remaining_volume_ml": round(remaining_volume_ml, 2),
            "percentage": round(percentage, 1),
            "drips_per_minute": round(drips_per_minute, 1),
            "remaining_time_minutes": round(remaining_time_minutes, 1),
            "time_elapsed_minutes": round(time_elapsed / 60, 1)
        }